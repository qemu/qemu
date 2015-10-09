/*
 * QTest testcase for the vhost-user
 *
 * Copyright (c) 2014 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include <glib.h>

#include "libqtest.h"
#include "qemu/option.h"
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
#define QEMU_CMD_MEM    " -m 512 -object memory-backend-file,id=mem,size=512M,"\
                        "mem-path=%s,share=on -numa node,memdev=mem"
#define QEMU_CMD_CHR    " -chardev socket,id=chr0,path=%s"
#define QEMU_CMD_NETDEV " -netdev vhost-user,id=net0,chardev=chr0,vhostforce"
#define QEMU_CMD_NET    " -device virtio-net-pci,netdev=net0 "
#define QEMU_CMD_ROM    " -option-rom ../pc-bios/pxe-virtio.rom"

#define QEMU_CMD        QEMU_CMD_ACCEL QEMU_CMD_MEM QEMU_CMD_CHR \
                        QEMU_CMD_NETDEV QEMU_CMD_NET QEMU_CMD_ROM

#define HUGETLBFS_MAGIC       0x958458f6

/*********** FROM hw/virtio/vhost-user.c *************************************/

#define VHOST_MEMORY_MAX_NREGIONS    8

#define VHOST_USER_F_PROTOCOL_FEATURES 30

typedef enum VhostUserRequest {
    VHOST_USER_NONE = 0,
    VHOST_USER_GET_FEATURES = 1,
    VHOST_USER_SET_FEATURES = 2,
    VHOST_USER_SET_OWNER = 3,
    VHOST_USER_RESET_DEVICE = 4,
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

typedef struct VhostUserMsg {
    VhostUserRequest request;

#define VHOST_USER_VERSION_MASK     (0x3)
#define VHOST_USER_REPLY_MASK       (0x1<<2)
    uint32_t flags;
    uint32_t size; /* the following payload size */
    union {
        uint64_t u64;
        struct vhost_vring_state state;
        struct vhost_vring_addr addr;
        VhostUserMemory memory;
    };
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
    gchar *chr_name;
    CharDriverState *chr;
    int fds_num;
    int fds[VHOST_MEMORY_MAX_NREGIONS];
    VhostUserMemory memory;
    GMutex data_mutex;
    GCond data_cond;
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

static void read_guest_mem(TestServer *s)
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
    GMainLoop *loop;
    loop = g_main_loop_new(NULL, FALSE);
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
        msg.size = sizeof(m.u64);
        msg.u64 = 0x1ULL << VHOST_USER_F_PROTOCOL_FEATURES;
        p = (uint8_t *) &msg;
        qemu_chr_fe_write_all(chr, p, VHOST_USER_HDR_SIZE + msg.size);
        break;

    case VHOST_USER_SET_FEATURES:
	g_assert_cmpint(msg.u64 & (0x1ULL << VHOST_USER_F_PROTOCOL_FEATURES),
			!=, 0ULL);
        break;

    case VHOST_USER_GET_PROTOCOL_FEATURES:
        /* send back features to qemu */
        msg.flags |= VHOST_USER_REPLY_MASK;
        msg.size = sizeof(m.u64);
        msg.u64 = 0;
        p = (uint8_t *) &msg;
        qemu_chr_fe_write_all(chr, p, VHOST_USER_HDR_SIZE + msg.size);
        break;

    case VHOST_USER_GET_VRING_BASE:
        /* send back vring base to qemu */
        msg.flags |= VHOST_USER_REPLY_MASK;
        msg.size = sizeof(m.state);
        msg.state.num = 0;
        p = (uint8_t *) &msg;
        qemu_chr_fe_write_all(chr, p, VHOST_USER_HDR_SIZE + msg.size);
        break;

    case VHOST_USER_SET_MEM_TABLE:
        /* received the mem table */
        memcpy(&s->memory, &msg.memory, sizeof(msg.memory));
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

static TestServer *test_server_new(const gchar *tmpfs, const gchar *name)
{
    TestServer *server = g_new0(TestServer, 1);
    gchar *chr_path;

    server->socket_path = g_strdup_printf("%s/%s.sock", tmpfs, name);

    chr_path = g_strdup_printf("unix:%s,server,nowait", server->socket_path);
    server->chr_name = g_strdup_printf("chr-%s", name);
    server->chr = qemu_chr_new(server->chr_name, chr_path, NULL);
    g_free(chr_path);

    qemu_chr_add_handlers(server->chr, chr_can_read, chr_read, NULL, server);

    g_mutex_init(&server->data_mutex);
    g_cond_init(&server->data_cond);

    return server;
}

#define GET_QEMU_CMD(s, root)                                \
    g_strdup_printf(QEMU_CMD, (root), (s)->socket_path)


static void test_server_free(TestServer *server)
{
    int i;

    qemu_chr_delete(server->chr);

    for (i = 0; i < server->fds_num; i++) {
        close(server->fds[i]);
    }

    unlink(server->socket_path);
    g_free(server->socket_path);

    g_free(server);
}

int main(int argc, char **argv)
{
    QTestState *s = NULL;
    TestServer *server = NULL;
    const char *hugefs;
    char *qemu_cmd = NULL;
    int ret;
    char template[] = "/tmp/vhost-test-XXXXXX";
    const char *tmpfs;
    const char *root;

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

    server = test_server_new(tmpfs, "test");

    /* run the main loop thread so the chardev may operate */
    g_thread_new(NULL, thread_function, NULL);

    qemu_cmd = GET_QEMU_CMD(server, root);

    s = qtest_start(qemu_cmd);
    g_free(qemu_cmd);

    qtest_add_data_func("/vhost-user/read-guest-mem", server, read_guest_mem);

    ret = g_test_run();

    if (s) {
        qtest_quit(s);
    }

    /* cleanup */
    test_server_free(server);

    ret = rmdir(tmpfs);
    if (ret != 0) {
        g_test_message("unable to rmdir: path (%s): %s\n",
                       tmpfs, strerror(errno));
    }
    g_assert_cmpint(ret, ==, 0);

    return ret;
}
