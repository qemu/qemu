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

#include "libqtest-single.h"
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "qemu/config-file.h"
#include "qemu/option.h"
#include "qemu/range.h"
#include "qemu/sockets.h"
#include "chardev/char-fe.h"
#include "qemu/memfd.h"
#include "qemu/module.h"
#include "sysemu/sysemu.h"
#include "libqos/libqos.h"
#include "libqos/pci-pc.h"
#include "libqos/virtio-pci.h"

#include "libqos/malloc-pc.h"
#include "libqos/qgraph_internal.h"
#include "hw/virtio/virtio-net.h"

#include "standard-headers/linux/vhost_types.h"
#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_net.h"
#include "standard-headers/linux/virtio_gpio.h"
#include "standard-headers/linux/virtio_scmi.h"

#ifdef CONFIG_LINUX
#include <sys/vfs.h>
#endif


#define QEMU_CMD_MEM    " -m %d -object memory-backend-file,id=mem,size=%dM," \
                        "mem-path=%s,share=on -numa node,memdev=mem"
#define QEMU_CMD_MEMFD  " -m %d -object memory-backend-memfd,id=mem,size=%dM," \
                        " -numa node,memdev=mem"
#define QEMU_CMD_CHR    " -chardev socket,id=%s,path=%s%s"
#define QEMU_CMD_NETDEV " -netdev vhost-user,id=hs0,chardev=%s,vhostforce=on"

#define HUGETLBFS_MAGIC       0x958458f6

/*********** FROM hw/virtio/vhost-user.c *************************************/

#define VHOST_MEMORY_MAX_NREGIONS    8
#define VHOST_MAX_VIRTQUEUES    0x100

#define VHOST_USER_F_PROTOCOL_FEATURES 30
#define VIRTIO_F_VERSION_1 32

#define VHOST_USER_PROTOCOL_F_MQ 0
#define VHOST_USER_PROTOCOL_F_LOG_SHMFD 1
#define VHOST_USER_PROTOCOL_F_CROSS_ENDIAN   6
#define VHOST_USER_PROTOCOL_F_CONFIG 9

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
    VHOST_USER_GET_CONFIG = 24,
    VHOST_USER_SET_CONFIG = 25,
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

enum {
    VHOST_USER_NET,
    VHOST_USER_GPIO,
    VHOST_USER_SCMI,
};

typedef struct TestServer {
    gchar *socket_path;
    gchar *mig_path;
    gchar *chr_name;
    gchar *tmpfs;
    CharBackend chr;
    int fds_num;
    int fds[VHOST_MEMORY_MAX_NREGIONS];
    VhostUserMemory memory;
    GMainContext *context;
    GMainLoop *loop;
    GThread *thread;
    GMutex data_mutex;
    GCond data_cond;
    int log_fd;
    uint64_t rings;
    bool test_fail;
    int test_flags;
    int queues;
    struct vhost_user_ops *vu_ops;
} TestServer;

struct vhost_user_ops {
    /* Device types. */
    int type;
    void (*append_opts)(TestServer *s, GString *cmd_line,
            const char *chr_opts);

    /* VHOST-USER commands. */
    uint64_t (*get_features)(TestServer *s);
    void (*set_features)(TestServer *s, CharBackend *chr,
                         VhostUserMsg *msg);
    void (*get_protocol_features)(TestServer *s,
                                  CharBackend *chr, VhostUserMsg *msg);
};

static const char *init_hugepagefs(void);
static TestServer *test_server_new(const gchar *name,
        struct vhost_user_ops *ops);
static void test_server_free(TestServer *server);
static void test_server_listen(TestServer *server);

enum test_memfd {
    TEST_MEMFD_AUTO,
    TEST_MEMFD_YES,
    TEST_MEMFD_NO,
};

static void append_vhost_net_opts(TestServer *s, GString *cmd_line,
                             const char *chr_opts)
{
    g_string_append_printf(cmd_line, QEMU_CMD_CHR QEMU_CMD_NETDEV,
                           s->chr_name, s->socket_path,
                           chr_opts, s->chr_name);
}

/*
 * For GPIO there are no other magic devices we need to add (like
 * block or netdev) so all we need to worry about is the vhost-user
 * chardev socket.
 */
static void append_vhost_gpio_opts(TestServer *s, GString *cmd_line,
                             const char *chr_opts)
{
    g_string_append_printf(cmd_line, QEMU_CMD_CHR,
                           s->chr_name, s->socket_path,
                           chr_opts);
}

static void append_mem_opts(TestServer *server, GString *cmd_line,
                            int size, enum test_memfd memfd)
{
    if (memfd == TEST_MEMFD_AUTO) {
        memfd = qemu_memfd_check(MFD_ALLOW_SEALING) ? TEST_MEMFD_YES
                                                    : TEST_MEMFD_NO;
    }

    if (memfd == TEST_MEMFD_YES) {
        g_string_append_printf(cmd_line, QEMU_CMD_MEMFD, size, size);
    } else {
        const char *root = init_hugepagefs() ? : server->tmpfs;

        g_string_append_printf(cmd_line, QEMU_CMD_MEM, size, size, root);
    }
}

static bool wait_for_fds(TestServer *s)
{
    gint64 end_time;
    bool got_region;
    int i;

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

    got_region = false;
    for (i = 0; i < s->memory.nregions; ++i) {
        VhostUserMemoryRegion *reg = &s->memory.regions[i];
        if (reg->guest_phys_addr == 0) {
            got_region = true;
            break;
        }
    }
    if (!got_region) {
        g_test_skip("No memory at address 0x0");
    }
    return got_region;
}

static void read_guest_mem_server(QTestState *qts, TestServer *s)
{
    uint8_t *guest_mem;
    int i, j;
    size_t size;

    g_mutex_lock(&s->data_mutex);

    /* iterate all regions */
    for (i = 0; i < s->fds_num; i++) {

        /* We'll check only the region starting at 0x0 */
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

        for (j = 0; j < 1024; j++) {
            uint32_t a = qtest_readb(qts, s->memory.regions[i].guest_phys_addr + j);
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
    g_autoptr(GError) err = NULL;
    TestServer *s = opaque;
    CharBackend *chr = &s->chr;
    VhostUserMsg msg;
    uint8_t *p = (uint8_t *) &msg;
    int fd = -1;

    if (s->test_fail) {
        qemu_chr_fe_disconnect(chr);
        /* now switch to non-failure */
        s->test_fail = false;
    }

    if (size != VHOST_USER_HDR_SIZE) {
        qos_printf("%s: Wrong message size received %d\n", __func__, size);
        return;
    }

    g_mutex_lock(&s->data_mutex);
    memcpy(p, buf, VHOST_USER_HDR_SIZE);

    if (msg.size) {
        p += VHOST_USER_HDR_SIZE;
        size = qemu_chr_fe_read_all(chr, p, msg.size);
        if (size != msg.size) {
            qos_printf("%s: Wrong message size received %d != %d\n",
                       __func__, size, msg.size);
            goto out;
        }
    }

    switch (msg.request) {
    case VHOST_USER_GET_FEATURES:
        /* Mandatory for tests to define get_features */
        g_assert(s->vu_ops->get_features);

        /* send back features to qemu */
        msg.flags |= VHOST_USER_REPLY_MASK;
        msg.size = sizeof(m.payload.u64);

        if (s->test_flags >= TEST_FLAGS_BAD) {
            msg.payload.u64 = 0;
            s->test_flags = TEST_FLAGS_END;
        } else {
            msg.payload.u64 = s->vu_ops->get_features(s);
        }

        qemu_chr_fe_write_all(chr, (uint8_t *) &msg,
                              VHOST_USER_HDR_SIZE + msg.size);
        break;

    case VHOST_USER_SET_FEATURES:
        if (s->vu_ops->set_features) {
            s->vu_ops->set_features(s, chr, &msg);
        }
        break;

    case VHOST_USER_SET_OWNER:
        /*
         * We don't need to do anything here, the remote is just
         * letting us know it is in charge. Just log it.
         */
        qos_printf("set_owner: start of session\n");
        break;

    case VHOST_USER_GET_PROTOCOL_FEATURES:
        if (s->vu_ops->get_protocol_features) {
            s->vu_ops->get_protocol_features(s, chr, &msg);
        }
        break;

    case VHOST_USER_GET_CONFIG:
        /*
         * Treat GET_CONFIG as a NOP and just reply and let the guest
         * consider we have updated its memory. Tests currently don't
         * require working configs.
         */
        msg.flags |= VHOST_USER_REPLY_MASK;
        p = (uint8_t *) &msg;
        qemu_chr_fe_write_all(chr, p, VHOST_USER_HDR_SIZE + msg.size);
        break;

    case VHOST_USER_SET_PROTOCOL_FEATURES:
        /*
         * We did set VHOST_USER_F_PROTOCOL_FEATURES so its valid for
         * the remote end to send this. There is no handshake reply so
         * just log the details for debugging.
         */
        qos_printf("set_protocol_features: 0x%"PRIx64 "\n", msg.payload.u64);
        break;

        /*
         * A real vhost-user backend would actually set the size and
         * address of the vrings but we can simply report them.
         */
    case VHOST_USER_SET_VRING_NUM:
        qos_printf("set_vring_num: %d/%d\n",
                   msg.payload.state.index, msg.payload.state.num);
        break;
    case VHOST_USER_SET_VRING_ADDR:
        qos_printf("set_vring_addr: 0x%"PRIx64"/0x%"PRIx64"/0x%"PRIx64"\n",
                   msg.payload.addr.avail_user_addr,
                   msg.payload.addr.desc_user_addr,
                   msg.payload.addr.used_user_addr);
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
        g_cond_broadcast(&s->data_cond);
        break;

    case VHOST_USER_SET_MEM_TABLE:
        /* received the mem table */
        memcpy(&s->memory, &msg.payload.memory, sizeof(msg.payload.memory));
        s->fds_num = qemu_chr_fe_get_msgfds(chr, s->fds,
                                            G_N_ELEMENTS(s->fds));

        /* signal the test that it can continue */
        g_cond_broadcast(&s->data_cond);
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
        g_unix_set_fd_nonblocking(fd, true, &err);
        g_assert_no_error(err);
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

        g_cond_broadcast(&s->data_cond);
        break;

    case VHOST_USER_SET_VRING_BASE:
        assert(msg.payload.state.index < s->queues * 2);
        s->rings |= 0x1ULL << msg.payload.state.index;
        g_cond_broadcast(&s->data_cond);
        break;

    case VHOST_USER_GET_QUEUE_NUM:
        msg.flags |= VHOST_USER_REPLY_MASK;
        msg.size = sizeof(m.payload.u64);
        msg.payload.u64 = s->queues;
        p = (uint8_t *) &msg;
        qemu_chr_fe_write_all(chr, p, VHOST_USER_HDR_SIZE + msg.size);
        break;

    case VHOST_USER_SET_VRING_ENABLE:
        /*
         * Another case we ignore as we don't need to respond. With a
         * fully functioning vhost-user we would enable/disable the
         * vring monitoring.
         */
        qos_printf("set_vring(%d)=%s\n", msg.payload.state.index,
                   msg.payload.state.num ? "enabled" : "disabled");
        break;

    default:
        qos_printf("vhost-user: un-handled message: %d\n", msg.request);
        break;
    }

out:
    g_mutex_unlock(&s->data_mutex);
}

static const char *init_hugepagefs(void)
{
#ifdef CONFIG_LINUX
    static const char *hugepagefs;
    const char *path = getenv("QTEST_HUGETLBFS_PATH");
    struct statfs fs;
    int ret;

    if (hugepagefs) {
        return hugepagefs;
    }
    if (!path) {
        return NULL;
    }

    if (access(path, R_OK | W_OK | X_OK)) {
        qos_printf("access on path (%s): %s", path, strerror(errno));
        g_test_fail();
        return NULL;
    }

    do {
        ret = statfs(path, &fs);
    } while (ret != 0 && errno == EINTR);

    if (ret != 0) {
        qos_printf("statfs on path (%s): %s", path, strerror(errno));
        g_test_fail();
        return NULL;
    }

    if (fs.f_type != HUGETLBFS_MAGIC) {
        qos_printf("Warning: path not on HugeTLBFS: %s", path);
        g_test_fail();
        return NULL;
    }

    hugepagefs = path;
    return hugepagefs;
#else
    return NULL;
#endif
}

static TestServer *test_server_new(const gchar *name,
        struct vhost_user_ops *ops)
{
    TestServer *server = g_new0(TestServer, 1);
    g_autofree const char *tmpfs = NULL;
    GError *err = NULL;

    server->context = g_main_context_new();
    server->loop = g_main_loop_new(server->context, FALSE);

    /* run the main loop thread so the chardev may operate */
    server->thread = g_thread_new(NULL, thread_function, server->loop);

    tmpfs = g_dir_make_tmp("vhost-test-XXXXXX", &err);
    if (!tmpfs) {
        g_test_message("Can't create temporary directory in %s: %s",
                       g_get_tmp_dir(), err->message);
        g_error_free(err);
    }
    g_assert(tmpfs);

    server->tmpfs = g_strdup(tmpfs);
    server->socket_path = g_strdup_printf("%s/%s.sock", tmpfs, name);
    server->mig_path = g_strdup_printf("%s/%s.mig", tmpfs, name);
    server->chr_name = g_strdup_printf("chr-%s", name);

    g_mutex_init(&server->data_mutex);
    g_cond_init(&server->data_cond);

    server->log_fd = -1;
    server->queues = 1;
    server->vu_ops = ops;

    return server;
}

static void chr_event(void *opaque, QEMUChrEvent event)
{
    TestServer *s = opaque;

    if (s->test_flags == TEST_FLAGS_END &&
        event == CHR_EVENT_CLOSED) {
        s->test_flags = TEST_FLAGS_OK;
    }
}

static void test_server_create_chr(TestServer *server, const gchar *opt)
{
    g_autofree gchar *chr_path = g_strdup_printf("unix:%s%s",
                                                 server->socket_path, opt);
    Chardev *chr;

    chr = qemu_chr_new(server->chr_name, chr_path, server->context);
    g_assert(chr);

    qemu_chr_fe_init(&server->chr, chr, &error_abort);
    qemu_chr_fe_set_handlers(&server->chr, chr_can_read, chr_read,
                             chr_event, NULL, server, server->context, true);
}

static void test_server_listen(TestServer *server)
{
    test_server_create_chr(server, ",server=on,wait=off");
}

static void test_server_free(TestServer *server)
{
    int i, ret;

    /* finish the helper thread and dispatch pending sources */
    g_main_loop_quit(server->loop);
    g_thread_join(server->thread);
    while (g_main_context_pending(NULL)) {
        g_main_context_iteration(NULL, TRUE);
    }

    unlink(server->socket_path);
    g_free(server->socket_path);

    unlink(server->mig_path);
    g_free(server->mig_path);

    ret = rmdir(server->tmpfs);
    if (ret != 0) {
        g_test_message("unable to rmdir: path (%s): %s",
                       server->tmpfs, strerror(errno));
    }
    g_free(server->tmpfs);

    qemu_chr_fe_deinit(&server->chr, true);

    for (i = 0; i < server->fds_num; i++) {
        close(server->fds[i]);
    }

    if (server->log_fd != -1) {
        close(server->log_fd);
    }

    g_free(server->chr_name);

    g_main_loop_unref(server->loop);
    g_main_context_unref(server->context);
    g_cond_clear(&server->data_cond);
    g_mutex_clear(&server->data_mutex);
    g_free(server);
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

static void vhost_user_test_cleanup(void *s)
{
    TestServer *server = s;

    qos_invalidate_command_line();
    test_server_free(server);
}

static void *vhost_user_test_setup(GString *cmd_line, void *arg)
{
    TestServer *server = test_server_new("vhost-user-test", arg);
    test_server_listen(server);

    append_mem_opts(server, cmd_line, 256, TEST_MEMFD_AUTO);
    server->vu_ops->append_opts(server, cmd_line, "");

    g_test_queue_destroy(vhost_user_test_cleanup, server);

    return server;
}

static void *vhost_user_test_setup_memfd(GString *cmd_line, void *arg)
{
    TestServer *server = test_server_new("vhost-user-test", arg);
    test_server_listen(server);

    append_mem_opts(server, cmd_line, 256, TEST_MEMFD_YES);
    server->vu_ops->append_opts(server, cmd_line, "");

    g_test_queue_destroy(vhost_user_test_cleanup, server);

    return server;
}

static void test_read_guest_mem(void *obj, void *arg, QGuestAllocator *alloc)
{
    TestServer *server = arg;

    if (!wait_for_fds(server)) {
        return;
    }

    read_guest_mem_server(global_qtest, server);
}

static void test_migrate(void *obj, void *arg, QGuestAllocator *alloc)
{
    TestServer *s = arg;
    TestServer *dest;
    GString *dest_cmdline;
    char *uri;
    QTestState *to;
    GSource *source;
    QDict *rsp;
    guint8 *log;
    guint64 size;

    if (!wait_for_fds(s)) {
        return;
    }

    dest = test_server_new("dest", s->vu_ops);
    dest_cmdline = g_string_new(qos_get_current_command_line());
    uri = g_strdup_printf("%s%s", "unix:", dest->mig_path);

    size = get_log_size(s);
    g_assert_cmpint(size, ==, (256 * 1024 * 1024) / (VHOST_LOG_PAGE * 8));

    test_server_listen(dest);
    g_string_append_printf(dest_cmdline, " -incoming %s", uri);
    append_mem_opts(dest, dest_cmdline, 256, TEST_MEMFD_AUTO);
    dest->vu_ops->append_opts(dest, dest_cmdline, "");
    to = qtest_init(dest_cmdline->str);

    /* This would be where you call qos_allocate_objects(to, NULL), if you want
     * to talk to the QVirtioNet object on the destination.
     */

    source = g_source_new(&test_migrate_source_funcs,
                          sizeof(TestMigrateSource));
    ((TestMigrateSource *)source)->src = s;
    ((TestMigrateSource *)source)->dest = dest;
    g_source_attach(source, s->context);

    /* slow down migration to have time to fiddle with log */
    /* TODO: qtest could learn to break on some places */
    rsp = qmp("{ 'execute': 'migrate-set-parameters',"
              "'arguments': { 'max-bandwidth': 10 } }");
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
    rsp = qmp("{ 'execute': 'migrate-set-parameters',"
              "'arguments': { 'max-bandwidth': 0 } }");
    g_assert(qdict_haskey(rsp, "return"));
    qobject_unref(rsp);

    qmp_eventwait("STOP");
    qtest_qmp_eventwait(to, "RESUME");

    g_assert(wait_for_fds(dest));
    read_guest_mem_server(to, dest);

    g_source_destroy(source);
    g_source_unref(source);

    qtest_quit(to);
    test_server_free(dest);
    g_free(uri);
    g_string_free(dest_cmdline, true);
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

static void *vhost_user_test_setup_reconnect(GString *cmd_line, void *arg)
{
    TestServer *s = test_server_new("reconnect", arg);

    g_thread_new("connect", connect_thread, s);
    append_mem_opts(s, cmd_line, 256, TEST_MEMFD_AUTO);
    s->vu_ops->append_opts(s, cmd_line, ",server=on");

    g_test_queue_destroy(vhost_user_test_cleanup, s);

    return s;
}

static void test_reconnect(void *obj, void *arg, QGuestAllocator *alloc)
{
    TestServer *s = arg;
    GSource *src;

    if (!wait_for_fds(s)) {
        return;
    }

    wait_for_rings_started(s, 2);

    /* reconnect */
    s->fds_num = 0;
    s->rings = 0;
    src = g_idle_source_new();
    g_source_set_callback(src, reconnect_cb, s, NULL);
    g_source_attach(src, s->context);
    g_source_unref(src);
    g_assert(wait_for_fds(s));
    wait_for_rings_started(s, 2);
}

static void *vhost_user_test_setup_connect_fail(GString *cmd_line, void *arg)
{
    TestServer *s = test_server_new("connect-fail", arg);

    s->test_fail = true;

    g_thread_new("connect", connect_thread, s);
    append_mem_opts(s, cmd_line, 256, TEST_MEMFD_AUTO);
    s->vu_ops->append_opts(s, cmd_line, ",server=on");

    g_test_queue_destroy(vhost_user_test_cleanup, s);

    return s;
}

static void *vhost_user_test_setup_flags_mismatch(GString *cmd_line, void *arg)
{
    TestServer *s = test_server_new("flags-mismatch", arg);

    s->test_flags = TEST_FLAGS_DISCONNECT;

    g_thread_new("connect", connect_thread, s);
    append_mem_opts(s, cmd_line, 256, TEST_MEMFD_AUTO);
    s->vu_ops->append_opts(s, cmd_line, ",server=on");

    g_test_queue_destroy(vhost_user_test_cleanup, s);

    return s;
}

static void test_vhost_user_started(void *obj, void *arg, QGuestAllocator *alloc)
{
    TestServer *s = arg;

    if (!wait_for_fds(s)) {
        return;
    }
    wait_for_rings_started(s, 2);
}

static void *vhost_user_test_setup_multiqueue(GString *cmd_line, void *arg)
{
    TestServer *s = vhost_user_test_setup(cmd_line, arg);

    s->queues = 2;
    g_string_append_printf(cmd_line,
                           " -set netdev.hs0.queues=%d"
                           " -global virtio-net-pci.vectors=%d",
                           s->queues, s->queues * 2 + 2);

    return s;
}

static void test_multiqueue(void *obj, void *arg, QGuestAllocator *alloc)
{
    TestServer *s = arg;

    wait_for_rings_started(s, s->queues * 2);
}


static uint64_t vu_net_get_features(TestServer *s)
{
    uint64_t features = 0x1ULL << VHOST_F_LOG_ALL |
        0x1ULL << VHOST_USER_F_PROTOCOL_FEATURES;

    if (s->queues > 1) {
        features |= 0x1ULL << VIRTIO_NET_F_MQ;
    }

    return features;
}

static void vu_net_set_features(TestServer *s, CharBackend *chr,
                                VhostUserMsg *msg)
{
    g_assert(msg->payload.u64 & (0x1ULL << VHOST_USER_F_PROTOCOL_FEATURES));
    if (s->test_flags == TEST_FLAGS_DISCONNECT) {
        qemu_chr_fe_disconnect(chr);
        s->test_flags = TEST_FLAGS_BAD;
    }
}

static void vu_net_get_protocol_features(TestServer *s, CharBackend *chr,
        VhostUserMsg *msg)
{
    /* send back features to qemu */
    msg->flags |= VHOST_USER_REPLY_MASK;
    msg->size = sizeof(m.payload.u64);
    msg->payload.u64 = 1 << VHOST_USER_PROTOCOL_F_LOG_SHMFD;
    msg->payload.u64 |= 1 << VHOST_USER_PROTOCOL_F_CROSS_ENDIAN;
    if (s->queues > 1) {
        msg->payload.u64 |= 1 << VHOST_USER_PROTOCOL_F_MQ;
    }
    qemu_chr_fe_write_all(chr, (uint8_t *)msg, VHOST_USER_HDR_SIZE + msg->size);
}

/* Each VHOST-USER device should have its ops structure defined. */
static struct vhost_user_ops g_vu_net_ops = {
    .type = VHOST_USER_NET,

    .append_opts = append_vhost_net_opts,

    .get_features = vu_net_get_features,
    .set_features = vu_net_set_features,
    .get_protocol_features = vu_net_get_protocol_features,
};

static void register_vhost_user_test(void)
{
    QOSGraphTestOptions opts = {
        .before = vhost_user_test_setup,
        .subprocess = true,
        .arg = &g_vu_net_ops,
    };

    qemu_add_opts(&qemu_chardev_opts);

    qos_add_test("vhost-user/read-guest-mem/memfile",
                 "virtio-net",
                 test_read_guest_mem, &opts);

    if (qemu_memfd_check(MFD_ALLOW_SEALING)) {
        opts.before = vhost_user_test_setup_memfd;
        qos_add_test("vhost-user/read-guest-mem/memfd",
                     "virtio-net",
                     test_read_guest_mem, &opts);
    }

    qos_add_test("vhost-user/migrate",
                 "virtio-net",
                 test_migrate, &opts);

    opts.before = vhost_user_test_setup_reconnect;
    qos_add_test("vhost-user/reconnect", "virtio-net",
                 test_reconnect, &opts);

    opts.before = vhost_user_test_setup_connect_fail;
    qos_add_test("vhost-user/connect-fail", "virtio-net",
                 test_vhost_user_started, &opts);

    opts.before = vhost_user_test_setup_flags_mismatch;
    qos_add_test("vhost-user/flags-mismatch", "virtio-net",
                 test_vhost_user_started, &opts);

    opts.before = vhost_user_test_setup_multiqueue;
    opts.edge.extra_device_opts = "mq=on";
    qos_add_test("vhost-user/multiqueue",
                 "virtio-net",
                 test_multiqueue, &opts);
}
libqos_init(register_vhost_user_test);

static uint64_t vu_gpio_get_features(TestServer *s)
{
    return 0x1ULL << VIRTIO_F_VERSION_1 |
        0x1ULL << VIRTIO_GPIO_F_IRQ |
        0x1ULL << VHOST_USER_F_PROTOCOL_FEATURES;
}

/*
 * This stub can't handle all the message types but we should reply
 * that we support VHOST_USER_PROTOCOL_F_CONFIG as gpio would use it
 * talking to a read vhost-user daemon.
 */
static void vu_gpio_get_protocol_features(TestServer *s, CharBackend *chr,
                                          VhostUserMsg *msg)
{
    /* send back features to qemu */
    msg->flags |= VHOST_USER_REPLY_MASK;
    msg->size = sizeof(m.payload.u64);
    msg->payload.u64 = 1ULL << VHOST_USER_PROTOCOL_F_CONFIG;

    qemu_chr_fe_write_all(chr, (uint8_t *)msg, VHOST_USER_HDR_SIZE + msg->size);
}

static struct vhost_user_ops g_vu_gpio_ops = {
    .type = VHOST_USER_GPIO,

    .append_opts = append_vhost_gpio_opts,

    .get_features = vu_gpio_get_features,
    .set_features = vu_net_set_features,
    .get_protocol_features = vu_gpio_get_protocol_features,
};

static void register_vhost_gpio_test(void)
{
    QOSGraphTestOptions opts = {
        .before = vhost_user_test_setup,
        .subprocess = true,
        .arg = &g_vu_gpio_ops,
    };

    qemu_add_opts(&qemu_chardev_opts);

    qos_add_test("read-guest-mem/memfile",
                 "vhost-user-gpio", test_read_guest_mem, &opts);
}
libqos_init(register_vhost_gpio_test);

static uint64_t vu_scmi_get_features(TestServer *s)
{
    return 0x1ULL << VIRTIO_F_VERSION_1 |
        0x1ULL << VIRTIO_SCMI_F_P2A_CHANNELS |
        0x1ULL << VHOST_USER_F_PROTOCOL_FEATURES;
}

static void vu_scmi_get_protocol_features(TestServer *s, CharBackend *chr,
                                          VhostUserMsg *msg)
{
    msg->flags |= VHOST_USER_REPLY_MASK;
    msg->size = sizeof(m.payload.u64);
    msg->payload.u64 = 1ULL << VHOST_USER_PROTOCOL_F_MQ;

    qemu_chr_fe_write_all(chr, (uint8_t *)msg, VHOST_USER_HDR_SIZE + msg->size);
}

static struct vhost_user_ops g_vu_scmi_ops = {
    .type = VHOST_USER_SCMI,

    .append_opts = append_vhost_gpio_opts,

    .get_features = vu_scmi_get_features,
    .set_features = vu_net_set_features,
    .get_protocol_features = vu_scmi_get_protocol_features,
};

static void register_vhost_scmi_test(void)
{
    QOSGraphTestOptions opts = {
        .before = vhost_user_test_setup,
        .subprocess = true,
        .arg = &g_vu_scmi_ops,
    };

    qemu_add_opts(&qemu_chardev_opts);

    qos_add_test("scmi/read-guest-mem/memfile",
                 "vhost-user-scmi", test_read_guest_mem, &opts);
}
libqos_init(register_vhost_scmi_test);
