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

#include "libqtest.h"
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "qemu/config-file.h"
#include "qemu/option.h"
#include "qemu/range.h"
#include "qemu/sockets.h"
#include "chardev/char-fe.h"
#include "qemu/memfd.h"
#include "sysemu/sysemu.h"
#include "libqos/libqos.h"
#include "libqos/pci-pc.h"
#include "libqos/virtio-pci.h"

#include "libqos/malloc-pc.h"
#include "hw/virtio/virtio-net.h"

#include <linux/vhost.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_net.h>
#include <sys/vfs.h>


#define QEMU_CMD_MEM    " -m %d -object memory-backend-file,id=mem,size=%dM," \
                        "mem-path=%s,share=on -numa node,memdev=mem"
#define QEMU_CMD_MEMFD  " -m %d -object memory-backend-memfd,id=mem,size=%dM," \
                        " -numa node,memdev=mem"
#define QEMU_CMD_CHR    " -chardev socket,id=%s,path=%s%s"
#define QEMU_CMD_NETDEV " -netdev vhost-user,id=net0,chardev=%s,vhostforce"
#define QEMU_CMD_NET    " -device virtio-net-pci,netdev=net0"

#define HUGETLBFS_MAGIC       0x958458f6

/*********** FROM hw/virtio/vhost-user.c *************************************/

#define VHOST_MEMORY_MAX_NREGIONS    8
#define VHOST_MAX_VIRTQUEUES    0x100

#define VHOST_USER_F_PROTOCOL_FEATURES 30
#define VHOST_USER_PROTOCOL_F_MQ 0
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
    VHOST_USER_GET_QUEUE_NUM = 17,
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

enum {
    TEST_FLAGS_OK,
    TEST_FLAGS_DISCONNECT,
    TEST_FLAGS_BAD,
    TEST_FLAGS_END,
};

typedef struct TestServer {
    QPCIBus *bus;
    QVirtioPCIDevice *dev;
    QVirtQueue *vq[VHOST_MAX_VIRTQUEUES];
    gchar *socket_path;
    gchar *mig_path;
    gchar *chr_name;
    CharBackend chr;
    int fds_num;
    int fds[VHOST_MEMORY_MAX_NREGIONS];
    VhostUserMemory memory;
    GMutex data_mutex;
    GCond data_cond;
    int log_fd;
    uint64_t rings;
    bool test_fail;
    int test_flags;
    int queues;
    QGuestAllocator *alloc;
} TestServer;

static TestServer *test_server_new(const gchar *name);
static void test_server_free(TestServer *server);
static void test_server_listen(TestServer *server);

static const char *tmpfs;
static const char *root;

enum test_memfd {
    TEST_MEMFD_AUTO,
    TEST_MEMFD_YES,
    TEST_MEMFD_NO,
};

static char *get_qemu_cmd(TestServer *s,
                          int mem, enum test_memfd memfd, const char *mem_path,
                          const char *chr_opts, const char *extra)
{
    if (memfd == TEST_MEMFD_AUTO && qemu_memfd_check(0)) {
        memfd = TEST_MEMFD_YES;
    }

    if (memfd == TEST_MEMFD_YES) {
        return g_strdup_printf(QEMU_CMD_MEMFD QEMU_CMD_CHR
                               QEMU_CMD_NETDEV QEMU_CMD_NET "%s", mem, mem,
                               s->chr_name, s->socket_path,
                               chr_opts, s->chr_name, extra);
    } else {
        return g_strdup_printf(QEMU_CMD_MEM QEMU_CMD_CHR
                               QEMU_CMD_NETDEV QEMU_CMD_NET "%s", mem, mem,
                               mem_path, s->chr_name, s->socket_path,
                               chr_opts, s->chr_name, extra);
    }
}

static void init_virtio_dev(TestServer *s, uint32_t features_mask)
{
    uint32_t features;
    int i;

    s->bus = qpci_init_pc(global_qtest, NULL);
    g_assert_nonnull(s->bus);

    s->dev = qvirtio_pci_device_find(s->bus, VIRTIO_ID_NET);
    g_assert_nonnull(s->dev);

    qvirtio_pci_device_enable(s->dev);
    qvirtio_reset(&s->dev->vdev);
    qvirtio_set_acknowledge(&s->dev->vdev);
    qvirtio_set_driver(&s->dev->vdev);

    s->alloc = pc_alloc_init(global_qtest);

    for (i = 0; i < s->queues * 2; i++) {
        s->vq[i] = qvirtqueue_setup(&s->dev->vdev, s->alloc, i);
    }

    features = qvirtio_get_features(&s->dev->vdev);
    features = features & features_mask;
    qvirtio_set_features(&s->dev->vdev, features);

    qvirtio_set_driver_ok(&s->dev->vdev);
}

static void uninit_virtio_dev(TestServer *s)
{
    int i;

    for (i = 0; i < s->queues * 2; i++) {
        qvirtqueue_cleanup(s->dev->vdev.bus, s->vq[i], s->alloc);
    }
    pc_alloc_uninit(s->alloc);

    qvirtio_pci_device_free(s->dev);
}

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

static void read_guest_mem_server(TestServer *s)
{
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
    CharBackend *chr = &s->chr;
    VhostUserMsg msg;
    uint8_t *p = (uint8_t *) &msg;
    int fd;

    if (s->test_fail) {
        qemu_chr_fe_disconnect(chr);
        /* now switch to non-failure */
        s->test_fail = false;
    }

    if (size != VHOST_USER_HDR_SIZE) {
        g_test_message("Wrong message size received %d\n", size);
        return;
    }

    g_mutex_lock(&s->data_mutex);
    memcpy(p, buf, VHOST_USER_HDR_SIZE);

    if (msg.size) {
        p += VHOST_USER_HDR_SIZE;
        size = qemu_chr_fe_read_all(chr, p, msg.size);
        if (size != msg.size) {
            g_test_message("Wrong message size received %d != %d\n",
                           size, msg.size);
            return;
        }
    }

    switch (msg.request) {
    case VHOST_USER_GET_FEATURES:
        /* send back features to qemu */
        msg.flags |= VHOST_USER_REPLY_MASK;
        msg.size = sizeof(m.payload.u64);
        msg.payload.u64 = 0x1ULL << VHOST_F_LOG_ALL |
            0x1ULL << VHOST_USER_F_PROTOCOL_FEATURES;
        if (s->queues > 1) {
            msg.payload.u64 |= 0x1ULL << VIRTIO_NET_F_MQ;
        }
        if (s->test_flags >= TEST_FLAGS_BAD) {
            msg.payload.u64 = 0;
            s->test_flags = TEST_FLAGS_END;
        }
        p = (uint8_t *) &msg;
        qemu_chr_fe_write_all(chr, p, VHOST_USER_HDR_SIZE + msg.size);
        break;

    case VHOST_USER_SET_FEATURES:
	g_assert_cmpint(msg.payload.u64 & (0x1ULL << VHOST_USER_F_PROTOCOL_FEATURES),
			!=, 0ULL);
        if (s->test_flags == TEST_FLAGS_DISCONNECT) {
            qemu_chr_fe_disconnect(chr);
            s->test_flags = TEST_FLAGS_BAD;
        }
        break;

    case VHOST_USER_GET_PROTOCOL_FEATURES:
        /* send back features to qemu */
        msg.flags |= VHOST_USER_REPLY_MASK;
        msg.size = sizeof(m.payload.u64);
        msg.payload.u64 = 1 << VHOST_USER_PROTOCOL_F_LOG_SHMFD;
        if (s->queues > 1) {
            msg.payload.u64 |= 1 << VHOST_USER_PROTOCOL_F_MQ;
        }
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

        assert(msg.payload.state.index < s->queues * 2);
        s->rings &= ~(0x1ULL << msg.payload.state.index);
        break;

    case VHOST_USER_SET_MEM_TABLE:
        /* received the mem table */
        memcpy(&s->memory, &msg.payload.memory, sizeof(msg.payload.memory));
        s->fds_num = qemu_chr_fe_get_msgfds(chr, s->fds,
                                            G_N_ELEMENTS(s->fds));

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
        assert(msg.payload.state.index < s->queues * 2);
        s->rings |= 0x1ULL << msg.payload.state.index;
        break;

    case VHOST_USER_GET_QUEUE_NUM:
        msg.flags |= VHOST_USER_REPLY_MASK;
        msg.size = sizeof(m.payload.u64);
        msg.payload.u64 = s->queues;
        p = (uint8_t *) &msg;
        qemu_chr_fe_write_all(chr, p, VHOST_USER_HDR_SIZE + msg.size);
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

    server->socket_path = g_strdup_printf("%s/%s.sock", tmpfs, name);
    server->mig_path = g_strdup_printf("%s/%s.mig", tmpfs, name);
    server->chr_name = g_strdup_printf("chr-%s", name);

    g_mutex_init(&server->data_mutex);
    g_cond_init(&server->data_cond);

    server->log_fd = -1;
    server->queues = 1;

    return server;
}

static void chr_event(void *opaque, int event)
{
    TestServer *s = opaque;

    if (s->test_flags == TEST_FLAGS_END &&
        event == CHR_EVENT_CLOSED) {
        s->test_flags = TEST_FLAGS_OK;
    }
}

static void test_server_create_chr(TestServer *server, const gchar *opt)
{
    gchar *chr_path;
    Chardev *chr;

    chr_path = g_strdup_printf("unix:%s%s", server->socket_path, opt);
    chr = qemu_chr_new(server->chr_name, chr_path);
    g_free(chr_path);

    g_assert_nonnull(chr);
    qemu_chr_fe_init(&server->chr, chr, &error_abort);
    qemu_chr_fe_set_handlers(&server->chr, chr_can_read, chr_read,
                             chr_event, NULL, server, NULL, true);
}

static void test_server_listen(TestServer *server)
{
    test_server_create_chr(server, ",server,nowait");
}

static gboolean _test_server_free(TestServer *server)
{
    int i;

    qemu_chr_fe_deinit(&server->chr, true);

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
    g_assert(server->bus);
    qpci_free_pc(server->bus);

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

GSourceFuncs test_migrate_source_funcs = {
    .check = test_migrate_source_check,
};

static void test_read_guest_mem(const void *arg)
{
    enum test_memfd memfd = GPOINTER_TO_INT(arg);
    TestServer *server = NULL;
    char *qemu_cmd = NULL;
    QTestState *s = NULL;

    server = test_server_new(memfd == TEST_MEMFD_YES ?
                             "read-guest-memfd" : "read-guest-mem");
    test_server_listen(server);

    qemu_cmd = get_qemu_cmd(server, 512, memfd, root, "", "");

    s = qtest_start(qemu_cmd);
    g_free(qemu_cmd);

    init_virtio_dev(server, 1u << VIRTIO_NET_F_MAC);

    read_guest_mem_server(server);

    uninit_virtio_dev(server);

    qtest_quit(s);
    test_server_free(server);
}

static void test_migrate(void)
{
    TestServer *s = test_server_new("src");
    TestServer *dest = test_server_new("dest");
    char *uri = g_strdup_printf("%s%s", "unix:", dest->mig_path);
    QTestState *global = global_qtest, *from, *to;
    GSource *source;
    gchar *cmd, *tmp;
    QDict *rsp;
    guint8 *log;
    guint64 size;

    test_server_listen(s);
    test_server_listen(dest);

    cmd = get_qemu_cmd(s, 2, TEST_MEMFD_AUTO, root, "", "");
    from = qtest_start(cmd);
    g_free(cmd);

    init_virtio_dev(s, 1u << VIRTIO_NET_F_MAC);
    init_virtio_dev(dest, 1u << VIRTIO_NET_F_MAC);
    wait_for_fds(s);
    size = get_log_size(s);
    g_assert_cmpint(size, ==, (2 * 1024 * 1024) / (VHOST_LOG_PAGE * 8));

    tmp = g_strdup_printf(" -incoming %s", uri);
    cmd = get_qemu_cmd(dest, 2, TEST_MEMFD_AUTO, root, "", tmp);
    g_free(tmp);
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
    qobject_unref(rsp);

    rsp = qmp("{ 'execute': 'migrate', 'arguments': { 'uri': %s } }", uri);
    g_assert(qdict_haskey(rsp, "return"));
    qobject_unref(rsp);

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
    qobject_unref(rsp);

    qmp_eventwait("STOP");

    global_qtest = to;
    qmp_eventwait("RESUME");

    read_guest_mem_server(dest);

    uninit_virtio_dev(s);
    uninit_virtio_dev(dest);

    g_source_destroy(source);
    g_source_unref(source);

    qtest_quit(to);
    test_server_free(dest);
    qtest_quit(from);
    test_server_free(s);
    g_free(uri);

    global_qtest = global;
}

static void wait_for_rings_started(TestServer *s, size_t count)
{
    gint64 end_time;

    g_mutex_lock(&s->data_mutex);
    end_time = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (ctpop64(s->rings) != count) {
        if (!g_cond_wait_until(&s->data_cond, &s->data_mutex, end_time)) {
            /* timeout has passed */
            g_assert_cmpint(ctpop64(s->rings), ==, count);
            break;
        }
    }

    g_mutex_unlock(&s->data_mutex);
}

static inline void test_server_connect(TestServer *server)
{
    test_server_create_chr(server, ",reconnect=1");
}

static gboolean
reconnect_cb(gpointer user_data)
{
    TestServer *s = user_data;

    qemu_chr_fe_disconnect(&s->chr);

    return FALSE;
}

static gpointer
connect_thread(gpointer data)
{
    TestServer *s = data;

    /* wait for qemu to start before first try, to avoid extra warnings */
    g_usleep(G_USEC_PER_SEC);
    test_server_connect(s);

    return NULL;
}

static void test_reconnect_subprocess(void)
{
    TestServer *s = test_server_new("reconnect");
    char *cmd;

    g_thread_new("connect", connect_thread, s);
    cmd = get_qemu_cmd(s, 2, TEST_MEMFD_AUTO, root, ",server", "");
    qtest_start(cmd);
    g_free(cmd);

    init_virtio_dev(s, 1u << VIRTIO_NET_F_MAC);
    wait_for_fds(s);
    wait_for_rings_started(s, 2);

    /* reconnect */
    s->fds_num = 0;
    s->rings = 0;
    g_idle_add(reconnect_cb, s);
    wait_for_fds(s);
    wait_for_rings_started(s, 2);

    uninit_virtio_dev(s);

    qtest_end();
    test_server_free(s);
    return;
}

static void test_reconnect(void)
{
    gchar *path = g_strdup_printf("/%s/vhost-user/reconnect/subprocess",
                                  qtest_get_arch());
    g_test_trap_subprocess(path, 0, 0);
    g_test_trap_assert_passed();
    g_free(path);
}

static void test_connect_fail_subprocess(void)
{
    TestServer *s = test_server_new("connect-fail");
    char *cmd;

    s->test_fail = true;
    g_thread_new("connect", connect_thread, s);
    cmd = get_qemu_cmd(s, 2, TEST_MEMFD_AUTO, root, ",server", "");
    qtest_start(cmd);
    g_free(cmd);

    init_virtio_dev(s, 1u << VIRTIO_NET_F_MAC);
    wait_for_fds(s);
    wait_for_rings_started(s, 2);

    uninit_virtio_dev(s);

    qtest_end();
    test_server_free(s);
}

static void test_connect_fail(void)
{
    gchar *path = g_strdup_printf("/%s/vhost-user/connect-fail/subprocess",
                                  qtest_get_arch());
    g_test_trap_subprocess(path, 0, 0);
    g_test_trap_assert_passed();
    g_free(path);
}

static void test_flags_mismatch_subprocess(void)
{
    TestServer *s = test_server_new("flags-mismatch");
    char *cmd;

    s->test_flags = TEST_FLAGS_DISCONNECT;
    g_thread_new("connect", connect_thread, s);
    cmd = get_qemu_cmd(s, 2, TEST_MEMFD_AUTO, root, ",server", "");
    qtest_start(cmd);
    g_free(cmd);

    init_virtio_dev(s, 1u << VIRTIO_NET_F_MAC);
    wait_for_fds(s);
    wait_for_rings_started(s, 2);

    uninit_virtio_dev(s);

    qtest_end();
    test_server_free(s);
}

static void test_flags_mismatch(void)
{
    gchar *path = g_strdup_printf("/%s/vhost-user/flags-mismatch/subprocess",
                                  qtest_get_arch());
    g_test_trap_subprocess(path, 0, 0);
    g_test_trap_assert_passed();
    g_free(path);
}


static void test_multiqueue(void)
{
    TestServer *s = test_server_new("mq");
    char *cmd;
    uint32_t features_mask = ~(QVIRTIO_F_BAD_FEATURE |
                            (1u << VIRTIO_RING_F_INDIRECT_DESC) |
                            (1u << VIRTIO_RING_F_EVENT_IDX));
    s->queues = 2;
    test_server_listen(s);

    if (qemu_memfd_check(0)) {
        cmd = g_strdup_printf(
            QEMU_CMD_MEMFD QEMU_CMD_CHR QEMU_CMD_NETDEV ",queues=%d "
            "-device virtio-net-pci,netdev=net0,mq=on,vectors=%d",
            512, 512, s->chr_name,
            s->socket_path, "", s->chr_name,
            s->queues, s->queues * 2 + 2);
    } else {
        cmd = g_strdup_printf(
            QEMU_CMD_MEM QEMU_CMD_CHR QEMU_CMD_NETDEV ",queues=%d "
            "-device virtio-net-pci,netdev=net0,mq=on,vectors=%d",
            512, 512, root, s->chr_name,
            s->socket_path, "", s->chr_name,
            s->queues, s->queues * 2 + 2);
    }
    qtest_start(cmd);
    g_free(cmd);

    init_virtio_dev(s, features_mask);

    wait_for_rings_started(s, s->queues * 2);

    uninit_virtio_dev(s);

    qtest_end();

    test_server_free(s);
}

int main(int argc, char **argv)
{
    const char *hugefs;
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

    loop = g_main_loop_new(NULL, FALSE);
    /* run the main loop thread so the chardev may operate */
    thread = g_thread_new(NULL, thread_function, loop);

    if (qemu_memfd_check(0)) {
        qtest_add_data_func("/vhost-user/read-guest-mem/memfd",
                            GINT_TO_POINTER(TEST_MEMFD_YES),
                            test_read_guest_mem);
    }
    qtest_add_data_func("/vhost-user/read-guest-mem/memfile",
                        GINT_TO_POINTER(TEST_MEMFD_NO), test_read_guest_mem);
    qtest_add_func("/vhost-user/migrate", test_migrate);
    qtest_add_func("/vhost-user/multiqueue", test_multiqueue);

    /* keeps failing on build-system since Aug 15 2017 */
    if (getenv("QTEST_VHOST_USER_FIXME")) {
        qtest_add_func("/vhost-user/reconnect/subprocess",
                       test_reconnect_subprocess);
        qtest_add_func("/vhost-user/reconnect", test_reconnect);
        qtest_add_func("/vhost-user/connect-fail/subprocess",
                       test_connect_fail_subprocess);
        qtest_add_func("/vhost-user/connect-fail", test_connect_fail);
        qtest_add_func("/vhost-user/flags-mismatch/subprocess",
                       test_flags_mismatch_subprocess);
        qtest_add_func("/vhost-user/flags-mismatch", test_flags_mismatch);
    }

    ret = g_test_run();

    /* cleanup */

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
