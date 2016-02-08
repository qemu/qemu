/*
 * QTest testcase for the vhost-user
 *
 * Copyright (c) 2014 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include <glib.h>

#include "libqtest.h"
#include "qemu/option.h"
#include "qemu/range.h"
#include "sysemu/char.h"
#include "sysemu/sysemu.h"

#include <linux/vhost.h>
#include <sys/mman.h>
#include <sys/vfs.h>
#include <qemu/sockets.h>

/* GLIB version compatibility flags */
#if !GLIB_CHECK_VERSION(2, 26, 0)
#define G_TIME_SPAN_SECOND              (G_GINT64_CONSTANT(1000000))
#endif

#if GLIB_CHECK_VERSION(2, 28, 0)
#define HAVE_MONOTONIC_TIME
#endif

#define QEMU_CMD_ACCEL  " -machine accel=tcg"
#define QEMU_CMD_MEM    " -m %d -object memory-backend-file,id=mem,size=%dM,"\
                        "mem-path=%s,share=on -numa node,memdev=mem"
#define QEMU_CMD_CHR    " -chardev socket,id=%s,path=%s"
#define QEMU_CMD_NETDEV " -netdev vhost-user,id=net0,chardev=%s,vhostforce"
#define QEMU_CMD_NET    " -device virtio-net-pci,netdev=net0,romfile=./pc-bios/pxe-virtio.rom"

#define QEMU_CMD        QEMU_CMD_ACCEL QEMU_CMD_MEM QEMU_CMD_CHR \
                        QEMU_CMD_NETDEV QEMU_CMD_NET

#define HUGETLBFS_MAGIC       0x958458f6

/*********** FROM hw/virtio/vhost-user.c *************************************/

#define VHOST_MEMORY_MAX_NREGIONS    8

#define VHOST_USER_F_PROTOCOL_FEATURES 30
#define VHOST_USER_PROTOCOL_F_LOG_SHMFD 1

#define VHOST_LOG_PAGE 0x1000

typedef enum VhostUserRequest {
    VHOST_USER_NONE = 0,
    VHOST_USER_GET_FEATURES = 1,
    VHOST_USER_SET_FEATURES = 2,
    VHOST_USER_SET_OWNER = 3,
    VHOST_USER_RESET_OWNER = 4,
    VHOST_USER_SET_MEM_TABLE = 5,
    VHOST_USER_SET_LOG_BASE = 6,
    VHOST_USER_SET_LOG_FD = 7,
    VHOST_USER_SET_VRING_NUM = 8,
    VHOST_USER_SET_VRING_ADDR = 9,
    VHOST_USER_SET_VRING_BASE = 10,
    VHOST_USER_GET_VRING_BASE = 11,
    VHOST_USER_SET_VRING_KICK = 12,
    VHOST_USER_SET_VRING_CALL = 13,
    VHOST_USER_SET_VRING_ERR = 14,
    VHOST_USER_GET_PROTOCOL_FEATURES = 15,
    VHOST_USER_SET_PROTOCOL_FEATURES = 16,
    VHOST_USER_SET_VRING_ENABLE = 18,
    VHOST_USER_MAX
} VhostUserRequest;

typedef struct VhostUserMemoryRegion {
    uint64_t guest_phys_addr;
    uint64_t memory_size;
    uint64_t userspace_addr;
    uint64_t mmap_offset;
} VhostUserMemoryRegion;

typedef struct VhostUserMemory {
    uint32_t nregions;
    uint32_t padding;
    VhostUserMemoryRegion regions[VHOST_MEMORY_MAX_NREGIONS];
} VhostUserMemory;

typedef struct VhostUserLog {
    uint64_t mmap_size;
    uint64_t mmap_offset;
} VhostUserLog;

typedef struct VhostUserMsg {
    VhostUserRequest request;

#define VHOST_USER_VERSION_MASK     (0x3)
#define VHOST_USER_REPLY_MASK       (0x1<<2)
    uint32_t flags;
    uint32_t size; /* the following payload size */
    union {
#define VHOST_USER_VRING_IDX_MASK   (0xff)
#define VHOST_USER_VRING_NOFD_MASK  (0x1<<8)
        uint64_t u64;
        struct vhost_vring_state state;
        struct vhost_vring_addr addr;
        VhostUserMemory memory;
        VhostUserLog log;
    } payload;
} QEMU_PACKED VhostUserMsg;

static VhostUserMsg m __attribute__ ((unused));
#define VHOST_USER_HDR_SIZE (sizeof(m.request) \
                            + sizeof(m.flags) \
                            + sizeof(m.size))

#define VHOST_USER_PAYLOAD_SIZE (sizeof(m) - VHOST_USER_HDR_SIZE)

/* The version of the protocol we support */
#define VHOST_USER_VERSION    (0x1)
/*****************************************************************************/

typedef struct TestServer {
    gchar *socket_path;
    gchar *mig_path;
    gchar *chr_name;
    CharDriverState *chr;
    int fds_num;
    int fds[VHOST_MEMORY_MAX_NREGIONS];
    VhostUserMemory memory;
    GMutex data_mutex;
    GCond data_cond;
    int log_fd;
    uint64_t rings;
} TestServer;

#if !GLIB_CHECK_VERSION(2, 32, 0)
static gboolean g_cond_wait_until(CompatGCond cond, CompatGMutex mutex,
                                  gint64 end_time)
{
    gboolean ret = FALSE;
    end_time -= g_get_monotonic_time();
    GTimeVal time = { end_time / G_TIME_SPAN_SECOND,
                      end_time % G_TIME_SPAN_SECOND };
    ret = g_cond_timed_wait(cond, mutex, &time);
    return ret;
}
#endif

static const char *tmpfs;
static const char *root;

static void wait_for_fds(TestServer *s)
{
    gint64 end_time;

    g_mutex_lock(&s->data_mutex);

    end_time = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (!s->fds_num) {
        if (!g_cond_wait_until(&s->data_cond, &s->data_mutex, end_time)) {
            /* timeout has passed */
            g_assert(s->fds_num);
            break;
        }
    }

    /* check for sanity */
    g_assert_cmpint(s->fds_num, >, 0);
    g_assert_cmpint(s->fds_num, ==, s->memory.nregions);

    g_mutex_unlock(&s->data_mutex);
}

static void read_guest_mem(const void *data)
{
    TestServer *s = (void *)data;
    uint32_t *guest_mem;
    int i, j;
    size_t size;

    wait_for_fds(s);

    g_mutex_lock(&s->data_mutex);

    /* iterate all regions */
    for (i = 0; i < s->fds_num; i++) {

        /* We'll check only the region statring at 0x0*/
        if (s->memory.regions[i].guest_phys_addr != 0x0) {
            continue;
        }

        g_assert_cmpint(s->memory.regions[i].memory_size, >, 1024);

        size = s->memory.regions[i].memory_size +
            s->memory.regions[i].mmap_offset;

        guest_mem = mmap(0, size, PROT_READ | PROT_WRITE,
                         MAP_SHARED, s->fds[i], 0);

        g_assert(guest_mem != MAP_FAILED);
        guest_mem += (s->memory.regions[i].mmap_offset / sizeof(*guest_mem));

        for (j = 0; j < 256; j++) {
            uint32_t a = readl(s->memory.regions[i].guest_phys_addr + j*4);
            uint32_t b = guest_mem[j];

            g_assert_cmpint(a, ==, b);
        }

        munmap(guest_mem, s->memory.regions[i].memory_size);
    }

    g_mutex_unlock(&s->data_mutex);
}

static void *thread_function(void *data)
{
    GMainLoop *loop = data;
    g_main_loop_run(loop);
    return NULL;
}

static int chr_can_read(void *opaque)
{
    return VHOST_USER_HDR_SIZE;
}

static void chr_read(void *opaque, const uint8_t *buf, int size)
{
    TestServer *s = opaque;
    CharDriverState *chr = s->chr;
    VhostUserMsg msg;
    uint8_t *p = (uint8_t *) &msg;
    int fd;

    if (size != VHOST_USER_HDR_SIZE) {
        g_test_message("Wrong message size received %d\n", size);
        return;
    }

    g_mutex_lock(&s->data_mutex);
    memcpy(p, buf, VHOST_USER_HDR_SIZE);

    if (msg.size) {
        p += VHOST_USER_HDR_SIZE;
        g_assert_cmpint(qemu_chr_fe_read_all(chr, p, msg.size), ==, msg.size);
    }

    switch (msg.request) {
    case VHOST_USER_GET_FEATURES:
        /* send back features to qemu */
        msg.flags |= VHOST_USER_REPLY_MASK;
        msg.size = sizeof(m.payload.u64);
        msg.payload.u64 = 0x1ULL << VHOST_F_LOG_ALL |
            0x1ULL << VHOST_USER_F_PROTOCOL_FEATURES;
        p = (uint8_t *) &msg;
        qemu_chr_fe_write_all(chr, p, VHOST_USER_HDR_SIZE + msg.size);
        break;

    case VHOST_USER_SET_FEATURES:
	g_assert_cmpint(msg.payload.u64 & (0x1ULL << VHOST_USER_F_PROTOCOL_FEATURES),
			!=, 0ULL);
        break;

    case VHOST_USER_GET_PROTOCOL_FEATURES:
        /* send back features to qemu */
        msg.flags |= VHOST_USER_REPLY_MASK;
        msg.size = sizeof(m.payload.u64);
        msg.payload.u64 = 1 << VHOST_USER_PROTOCOL_F_LOG_SHMFD;
        p = (uint8_t *) &msg;
        qemu_chr_fe_write_all(chr, p, VHOST_USER_HDR_SIZE + msg.size);
        break;

    case VHOST_USER_GET_VRING_BASE:
        /* send back vring base to qemu */
        msg.flags |= VHOST_USER_REPLY_MASK;
        msg.size = sizeof(m.payload.state);
        msg.payload.state.num = 0;
        p = (uint8_t *) &msg;
        qemu_chr_fe_write_all(chr, p, VHOST_USER_HDR_SIZE + msg.size);

        assert(msg.payload.state.index < 2);
        s->rings &= ~(0x1ULL << msg.payload.state.index);
        break;

    case VHOST_USER_SET_MEM_TABLE:
        /* received the mem table */
        memcpy(&s->memory, &msg.payload.memory, sizeof(msg.payload.memory));
        s->fds_num = qemu_chr_fe_get_msgfds(chr, s->fds, G_N_ELEMENTS(s->fds));

        /* signal the test that it can continue */
        g_cond_signal(&s->data_cond);
        break;

    case VHOST_USER_SET_VRING_KICK:
    case VHOST_USER_SET_VRING_CALL:
        /* consume the fd */
        qemu_chr_fe_get_msgfds(chr, &fd, 1);
        /*
         * This is a non-blocking eventfd.
         * The receive function forces it to be blocking,
         * so revert it back to non-blocking.
         */
        qemu_set_nonblock(fd);
        break;

    case VHOST_USER_SET_LOG_BASE:
        if (s->log_fd != -1) {
            close(s->log_fd);
            s->log_fd = -1;
        }
        qemu_chr_fe_get_msgfds(chr, &s->log_fd, 1);
        msg.flags |= VHOST_USER_REPLY_MASK;
        msg.size = 0;
        p = (uint8_t *) &msg;
        qemu_chr_fe_write_all(chr, p, VHOST_USER_HDR_SIZE);

        g_cond_signal(&s->data_cond);
        break;

    case VHOST_USER_SET_VRING_BASE:
        assert(msg.payload.state.index < 2);
        s->rings |= 0x1ULL << msg.payload.state.index;
        break;

    default:
        break;
    }

    g_mutex_unlock(&s->data_mutex);
}

static const char *init_hugepagefs(const char *path)
{
    struct statfs fs;
    int ret;

    if (access(path, R_OK | W_OK | X_OK)) {
        g_test_message("access on path (%s): %s\n", path, strerror(errno));
        return NULL;
    }

    do {
        ret = statfs(path, &fs);
    } while (ret != 0 && errno == EINTR);

    if (ret != 0) {
        g_test_message("statfs on path (%s): %s\n", path, strerror(errno));
        return NULL;
    }

    if (fs.f_type != HUGETLBFS_MAGIC) {
        g_test_message("Warning: path not on HugeTLBFS: %s\n", path);
        return NULL;
    }

    return path;
}

static TestServer *test_server_new(const gchar *name)
{
    TestServer *server = g_new0(TestServer, 1);
    gchar *chr_path;

    server->socket_path = g_strdup_printf("%s/%s.sock", tmpfs, name);
    server->mig_path = g_strdup_printf("%s/%s.mig", tmpfs, name);

    chr_path = g_strdup_printf("unix:%s,server,nowait", server->socket_path);
    server->chr_name = g_strdup_printf("chr-%s", name);
    server->chr = qemu_chr_new(server->chr_name, chr_path, NULL);
    g_free(chr_path);

    qemu_chr_add_handlers(server->chr, chr_can_read, chr_read, NULL, server);

    g_mutex_init(&server->data_mutex);
    g_cond_init(&server->data_cond);

    server->log_fd = -1;

    return server;
}

#define GET_QEMU_CMD(s)                                                        \
    g_strdup_printf(QEMU_CMD, 512, 512, (root), (s)->chr_name,                 \
                    (s)->socket_path, (s)->chr_name)

#define GET_QEMU_CMDE(s, mem, extra, ...)                                      \
    g_strdup_printf(QEMU_CMD extra, (mem), (mem), (root), (s)->chr_name,       \
                    (s)->socket_path, (s)->chr_name, ##__VA_ARGS__)

static gboolean _test_server_free(TestServer *server)
{
    int i;

    qemu_chr_delete(server->chr);

    for (i = 0; i < server->fds_num; i++) {
        close(server->fds[i]);
    }

    if (server->log_fd != -1) {
        close(server->log_fd);
    }

    unlink(server->socket_path);
    g_free(server->socket_path);

    unlink(server->mig_path);
    g_free(server->mig_path);

    g_free(server->chr_name);
    g_free(server);

    return FALSE;
}

static void test_server_free(TestServer *server)
{
    g_idle_add((GSourceFunc)_test_server_free, server);
}

static void wait_for_log_fd(TestServer *s)
{
    gint64 end_time;

    g_mutex_lock(&s->data_mutex);
    end_time = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (s->log_fd == -1) {
        if (!g_cond_wait_until(&s->data_cond, &s->data_mutex, end_time)) {
            /* timeout has passed */
            g_assert(s->log_fd != -1);
            break;
        }
    }

    g_mutex_unlock(&s->data_mutex);
}

static void write_guest_mem(TestServer *s, uint32_t seed)
{
    uint32_t *guest_mem;
    int i, j;
    size_t size;

    wait_for_fds(s);

    /* iterate all regions */
    for (i = 0; i < s->fds_num; i++) {

        /* We'll write only the region statring at 0x0 */
        if (s->memory.regions[i].guest_phys_addr != 0x0) {
            continue;
        }

        g_assert_cmpint(s->memory.regions[i].memory_size, >, 1024);

        size = s->memory.regions[i].memory_size +
            s->memory.regions[i].mmap_offset;

        guest_mem = mmap(0, size, PROT_READ | PROT_WRITE,
                         MAP_SHARED, s->fds[i], 0);

        g_assert(guest_mem != MAP_FAILED);
        guest_mem += (s->memory.regions[i].mmap_offset / sizeof(*guest_mem));

        for (j = 0; j < 256; j++) {
            guest_mem[j] = seed + j;
        }

        munmap(guest_mem, s->memory.regions[i].memory_size);
        break;
    }
}

static guint64 get_log_size(TestServer *s)
{
    guint64 log_size = 0;
    int i;

    for (i = 0; i < s->memory.nregions; ++i) {
        VhostUserMemoryRegion *reg = &s->memory.regions[i];
        guint64 last = range_get_last(reg->guest_phys_addr,
                                       reg->memory_size);
        log_size = MAX(log_size, last / (8 * VHOST_LOG_PAGE) + 1);
    }

    return log_size;
}

typedef struct TestMigrateSource {
    GSource source;
    TestServer *src;
    TestServer *dest;
} TestMigrateSource;

static gboolean
test_migrate_source_check(GSource *source)
{
    TestMigrateSource *t = (TestMigrateSource *)source;
    gboolean overlap = t->src->rings && t->dest->rings;

    g_assert(!overlap);

    return FALSE;
}

#if !GLIB_CHECK_VERSION(2,36,0)
/* this callback is unnecessary with glib >2.36, the default
 * prepare for the source does the same */
static gboolean
test_migrate_source_prepare(GSource *source, gint *timeout)
{
    *timeout = -1;
    return FALSE;
}
#endif

GSourceFuncs test_migrate_source_funcs = {
#if !GLIB_CHECK_VERSION(2,36,0)
    .prepare = test_migrate_source_prepare,
#endif
    .check = test_migrate_source_check,
};

static void test_migrate(void)
{
    TestServer *s = test_server_new("src");
    TestServer *dest = test_server_new("dest");
    char *uri = g_strdup_printf("%s%s", "unix:", dest->mig_path);
    QTestState *global = global_qtest, *from, *to;
    GSource *source;
    gchar *cmd;
    QDict *rsp;
    guint8 *log;
    guint64 size;

    cmd = GET_QEMU_CMDE(s, 2, "");
    from = qtest_start(cmd);
    g_free(cmd);

    wait_for_fds(s);
    size = get_log_size(s);
    g_assert_cmpint(size, ==, (2 * 1024 * 1024) / (VHOST_LOG_PAGE * 8));

    cmd = GET_QEMU_CMDE(dest, 2, " -incoming %s", uri);
    to = qtest_init(cmd);
    g_free(cmd);

    source = g_source_new(&test_migrate_source_funcs,
                          sizeof(TestMigrateSource));
    ((TestMigrateSource *)source)->src = s;
    ((TestMigrateSource *)source)->dest = dest;
    g_source_attach(source, NULL);

    /* slow down migration to have time to fiddle with log */
    /* TODO: qtest could learn to break on some places */
    rsp = qmp("{ 'execute': 'migrate_set_speed',"
              "'arguments': { 'value': 10 } }");
    g_assert(qdict_haskey(rsp, "return"));
    QDECREF(rsp);

    cmd = g_strdup_printf("{ 'execute': 'migrate',"
                          "'arguments': { 'uri': '%s' } }",
                          uri);
    rsp = qmp(cmd);
    g_free(cmd);
    g_assert(qdict_haskey(rsp, "return"));
    QDECREF(rsp);

    wait_for_log_fd(s);

    log = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, s->log_fd, 0);
    g_assert(log != MAP_FAILED);

    /* modify first page */
    write_guest_mem(s, 0x42);
    log[0] = 1;
    munmap(log, size);

    /* speed things up */
    rsp = qmp("{ 'execute': 'migrate_set_speed',"
              "'arguments': { 'value': 0 } }");
    g_assert(qdict_haskey(rsp, "return"));
    QDECREF(rsp);

    qmp_eventwait("STOP");

    global_qtest = to;
    qmp_eventwait("RESUME");

    read_guest_mem(dest);

    g_source_destroy(source);
    g_source_unref(source);

    qtest_quit(to);
    test_server_free(dest);
    qtest_quit(from);
    test_server_free(s);
    g_free(uri);

    global_qtest = global;
}

int main(int argc, char **argv)
{
    QTestState *s = NULL;
    TestServer *server = NULL;
    const char *hugefs;
    char *qemu_cmd = NULL;
    int ret;
    char template[] = "/tmp/vhost-test-XXXXXX";
    GMainLoop *loop;
    GThread *thread;

    g_test_init(&argc, &argv, NULL);

    module_call_init(MODULE_INIT_QOM);
    qemu_add_opts(&qemu_chardev_opts);

    tmpfs = mkdtemp(template);
    if (!tmpfs) {
        g_test_message("mkdtemp on path (%s): %s\n", template, strerror(errno));
    }
    g_assert(tmpfs);

    hugefs = getenv("QTEST_HUGETLBFS_PATH");
    if (hugefs) {
        root = init_hugepagefs(hugefs);
        g_assert(root);
    } else {
        root = tmpfs;
    }

    server = test_server_new("test");

    loop = g_main_loop_new(NULL, FALSE);
    /* run the main loop thread so the chardev may operate */
    thread = g_thread_new(NULL, thread_function, loop);

    qemu_cmd = GET_QEMU_CMD(server);

    s = qtest_start(qemu_cmd);
    g_free(qemu_cmd);

    qtest_add_data_func("/vhost-user/read-guest-mem", server, read_guest_mem);
    qtest_add_func("/vhost-user/migrate", test_migrate);

    ret = g_test_run();

    if (s) {
        qtest_quit(s);
    }

    /* cleanup */
    test_server_free(server);

    /* finish the helper thread and dispatch pending sources */
    g_main_loop_quit(loop);
    g_thread_join(thread);
    while (g_main_context_pending(NULL)) {
        g_main_context_iteration (NULL, TRUE);
    }
    g_main_loop_unref(loop);

    ret = rmdir(tmpfs);
    if (ret != 0) {
        g_test_message("unable to rmdir: path (%s): %s\n",
                       tmpfs, strerror(errno));
    }
    g_assert_cmpint(ret, ==, 0);

    return ret;
}
