/*
 * QTest testcase for TPM CRB
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * Authors:
 *   Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>

#include "hw/acpi/tpm.h"
#include "hw/tpm/tpm_ioctl.h"
#include "io/channel-socket.h"
#include "libqtest.h"
#include "qapi/error.h"

#define TPM_RC_FAILURE 0x101
#define TPM2_ST_NO_SESSIONS 0x8001

struct tpm_hdr {
    uint16_t tag;
    uint32_t len;
    uint32_t code; /*ordinal/error */
    char buffer[];
} QEMU_PACKED;

typedef struct TestState {
    CompatGMutex data_mutex;
    CompatGCond data_cond;
    SocketAddress *addr;
    QIOChannel *tpm_ioc;
    GThread *emu_tpm_thread;
    struct tpm_hdr *tpm_msg;
} TestState;

static void test_wait_cond(TestState *s)
{
    gint64 end_time = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;

    g_mutex_lock(&s->data_mutex);
    if (!g_cond_wait_until(&s->data_cond, &s->data_mutex, end_time)) {
        g_assert_not_reached();
    }
    g_mutex_unlock(&s->data_mutex);
}

static void *emu_tpm_thread(void *data)
{
    TestState *s = data;
    QIOChannel *ioc = s->tpm_ioc;

    s->tpm_msg = g_new(struct tpm_hdr, 1);
    while (true) {
        int minhlen = sizeof(s->tpm_msg->tag) + sizeof(s->tpm_msg->len);

        if (!qio_channel_read(ioc, (char *)s->tpm_msg, minhlen, &error_abort)) {
            break;
        }
        s->tpm_msg->tag = be16_to_cpu(s->tpm_msg->tag);
        s->tpm_msg->len = be32_to_cpu(s->tpm_msg->len);
        g_assert_cmpint(s->tpm_msg->len, >=, minhlen);
        g_assert_cmpint(s->tpm_msg->tag, ==, TPM2_ST_NO_SESSIONS);

        s->tpm_msg = g_realloc(s->tpm_msg, s->tpm_msg->len);
        qio_channel_read(ioc, (char *)&s->tpm_msg->code,
                         s->tpm_msg->len - minhlen, &error_abort);
        s->tpm_msg->code = be32_to_cpu(s->tpm_msg->code);

        /* reply error */
        s->tpm_msg->tag = cpu_to_be16(TPM2_ST_NO_SESSIONS);
        s->tpm_msg->len = cpu_to_be32(sizeof(struct tpm_hdr));
        s->tpm_msg->code = cpu_to_be32(TPM_RC_FAILURE);
        qio_channel_write(ioc, (char *)s->tpm_msg, be32_to_cpu(s->tpm_msg->len),
                          &error_abort);
    }

    g_free(s->tpm_msg);
    s->tpm_msg = NULL;
    object_unref(OBJECT(s->tpm_ioc));
    return NULL;
}

static void *emu_ctrl_thread(void *data)
{
    TestState *s = data;
    QIOChannelSocket *lioc = qio_channel_socket_new();
    QIOChannel *ioc;

    qio_channel_socket_listen_sync(lioc, s->addr, &error_abort);
    g_cond_signal(&s->data_cond);

    qio_channel_wait(QIO_CHANNEL(lioc), G_IO_IN);
    ioc = QIO_CHANNEL(qio_channel_socket_accept(lioc, &error_abort));
    g_assert(ioc);

    {
        uint32_t cmd = 0;
        struct iovec iov = { .iov_base = &cmd, .iov_len = sizeof(cmd) };
        int *pfd = NULL;
        size_t nfd = 0;

        qio_channel_readv_full(ioc, &iov, 1, &pfd, &nfd, &error_abort);
        cmd = be32_to_cpu(cmd);
        g_assert_cmpint(cmd, ==, CMD_SET_DATAFD);
        g_assert_cmpint(nfd, ==, 1);
        s->tpm_ioc = QIO_CHANNEL(qio_channel_socket_new_fd(*pfd, &error_abort));
        g_free(pfd);

        cmd = 0;
        qio_channel_write(ioc, (char *)&cmd, sizeof(cmd), &error_abort);

        s->emu_tpm_thread = g_thread_new(NULL, emu_tpm_thread, s);
    }

    while (true) {
        uint32_t cmd;
        ssize_t ret;

        ret = qio_channel_read(ioc, (char *)&cmd, sizeof(cmd), NULL);
        if (ret <= 0) {
            break;
        }

        cmd = be32_to_cpu(cmd);
        switch (cmd) {
        case CMD_GET_CAPABILITY: {
            ptm_cap cap = cpu_to_be64(0x3fff);
            qio_channel_write(ioc, (char *)&cap, sizeof(cap), &error_abort);
            break;
        }
        case CMD_INIT: {
            ptm_init init;
            qio_channel_read(ioc, (char *)&init.u.req, sizeof(init.u.req),
                              &error_abort);
            init.u.resp.tpm_result = 0;
            qio_channel_write(ioc, (char *)&init.u.resp, sizeof(init.u.resp),
                              &error_abort);
            break;
        }
        case CMD_SHUTDOWN: {
            ptm_res res = 0;
            qio_channel_write(ioc, (char *)&res, sizeof(res), &error_abort);
            qio_channel_close(s->tpm_ioc, &error_abort);
            g_thread_join(s->emu_tpm_thread);
            break;
        }
        case CMD_STOP: {
            ptm_res res = 0;
            qio_channel_write(ioc, (char *)&res, sizeof(res), &error_abort);
            break;
        }
        case CMD_SET_BUFFERSIZE: {
            ptm_setbuffersize sbs;
            qio_channel_read(ioc, (char *)&sbs.u.req, sizeof(sbs.u.req),
                             &error_abort);
            sbs.u.resp.buffersize = sbs.u.req.buffersize ?: cpu_to_be32(4096);
            sbs.u.resp.tpm_result = 0;
            sbs.u.resp.minsize = cpu_to_be32(128);
            sbs.u.resp.maxsize = cpu_to_be32(4096);
            qio_channel_write(ioc, (char *)&sbs.u.resp, sizeof(sbs.u.resp),
                              &error_abort);
            break;
        }
        case CMD_SET_LOCALITY: {
            ptm_loc loc;
            /* Note: this time it's not u.req / u.resp... */
            qio_channel_read(ioc, (char *)&loc, sizeof(loc), &error_abort);
            g_assert_cmpint(loc.u.req.loc, ==, 0);
            loc.u.resp.tpm_result = 0;
            qio_channel_write(ioc, (char *)&loc, sizeof(loc), &error_abort);
            break;
        }
        default:
            g_debug("unimplemented %u", cmd);
            g_assert_not_reached();
        }
    }

    object_unref(OBJECT(ioc));
    object_unref(OBJECT(lioc));
    return NULL;
}

#define TPM_CMD "\x80\x01\x00\x00\x00\x0c\x00\x00\x01\x44\x00\x00"

static void tpm_crb_test(const void *data)
{
    const TestState *s = data;
    uint32_t intfid = readl(TPM_CRB_ADDR_BASE + A_CRB_INTF_ID);
    uint32_t csize = readl(TPM_CRB_ADDR_BASE + A_CRB_CTRL_CMD_SIZE);
    uint64_t caddr = readq(TPM_CRB_ADDR_BASE + A_CRB_CTRL_CMD_LADDR);
    uint32_t rsize = readl(TPM_CRB_ADDR_BASE + A_CRB_CTRL_RSP_SIZE);
    uint64_t raddr = readq(TPM_CRB_ADDR_BASE + A_CRB_CTRL_RSP_ADDR);

    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, InterfaceType), ==, 1);
    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, InterfaceVersion), ==, 1);
    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, CapLocality), ==, 0);
    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, CapCRBIdleBypass), ==, 0);
    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, CapDataXferSizeSupport),
                    ==, 3);
    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, CapFIFO), ==, 0);
    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, CapCRB), ==, 1);
    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, InterfaceSelector), ==, 1);
    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, RID), ==, 0);

    g_assert_cmpint(csize, >=, 128);
    g_assert_cmpint(rsize, >=, 128);
    g_assert_cmpint(caddr, >, TPM_CRB_ADDR_BASE);
    g_assert_cmpint(raddr, >, TPM_CRB_ADDR_BASE);

    memwrite(caddr, TPM_CMD, sizeof(TPM_CMD));

    uint32_t sts, start = 1;
    uint64_t end_time = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    writel(TPM_CRB_ADDR_BASE + A_CRB_CTRL_START, start);
    do {
        start = readl(TPM_CRB_ADDR_BASE + A_CRB_CTRL_START);
        if ((start & 1) == 0) {
            break;
        }
    } while (g_get_monotonic_time() < end_time);
    start = readl(TPM_CRB_ADDR_BASE + A_CRB_CTRL_START);
    g_assert_cmpint(start & 1, ==, 0);
    sts = readl(TPM_CRB_ADDR_BASE + A_CRB_CTRL_STS);
    g_assert_cmpint(sts & 1, ==, 0);

    struct tpm_hdr tpm_msg;
    memread(raddr, &tpm_msg, sizeof(tpm_msg));
    g_assert_cmpmem(&tpm_msg, sizeof(tpm_msg), s->tpm_msg, sizeof(*s->tpm_msg));
}

int main(int argc, char **argv)
{
    int ret;
    char *args, *tmp_path = g_dir_make_tmp("qemu-tpm-crb-test.XXXXXX", NULL);
    GThread *thread;
    TestState test;

    module_call_init(MODULE_INIT_QOM);
    g_test_init(&argc, &argv, NULL);

    test.addr = g_new0(SocketAddress, 1);
    test.addr->type = SOCKET_ADDRESS_TYPE_UNIX;
    test.addr->u.q_unix.path = g_build_filename(tmp_path, "sock", NULL);
    g_mutex_init(&test.data_mutex);
    g_cond_init(&test.data_cond);

    thread = g_thread_new(NULL, emu_ctrl_thread, &test);
    test_wait_cond(&test);

    args = g_strdup_printf(
        "-chardev socket,id=chr,path=%s "
        "-tpmdev emulator,id=dev,chardev=chr "
        "-device tpm-crb,tpmdev=dev",
        test.addr->u.q_unix.path);
    qtest_start(args);

    qtest_add_data_func("/tpm-crb/test", &test, tpm_crb_test);
    ret = g_test_run();

    qtest_end();

    g_thread_join(thread);
    g_unlink(test.addr->u.q_unix.path);
    qapi_free_SocketAddress(test.addr);
    g_rmdir(tmp_path);
    g_free(tmp_path);
    g_free(args);
    return ret;
}
