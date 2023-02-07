/*
 * Minimal TPM emulator for TPM test cases
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

#include "backends/tpm/tpm_ioctl.h"
#include "io/channel-socket.h"
#include "qapi/error.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qstring.h"
#include "tpm-emu.h"

void tpm_emu_test_wait_cond(TPMTestState *s)
{
    gint64 end_time = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;

    g_mutex_lock(&s->data_mutex);

    if (!s->data_cond_signal &&
        !g_cond_wait_until(&s->data_cond, &s->data_mutex, end_time)) {
        g_assert_not_reached();
    }

    s->data_cond_signal = false;

    g_mutex_unlock(&s->data_mutex);
}

static void tpm_emu_close_ioc(void *ioc)
{
    qio_channel_close(ioc, NULL);
}

static void *tpm_emu_tpm_thread(void *data)
{
    TPMTestState *s = data;
    QIOChannel *ioc = s->tpm_ioc;

    qtest_add_abrt_handler(tpm_emu_close_ioc, ioc);

    s->tpm_msg = g_new(struct tpm_hdr, 1);
    while (true) {
        int minhlen = sizeof(s->tpm_msg->tag) + sizeof(s->tpm_msg->len);

        if (!qio_channel_read(ioc, (char *)s->tpm_msg, minhlen, &error_abort)) {
            break;
        }
        s->tpm_msg->tag = be16_to_cpu(s->tpm_msg->tag);
        s->tpm_msg->len = be32_to_cpu(s->tpm_msg->len);
        g_assert_cmpint(s->tpm_msg->len, >=, minhlen);

        s->tpm_msg = g_realloc(s->tpm_msg, s->tpm_msg->len);
        qio_channel_read(ioc, (char *)&s->tpm_msg->code,
                         s->tpm_msg->len - minhlen, &error_abort);
        s->tpm_msg->code = be32_to_cpu(s->tpm_msg->code);

        /* reply error */
        switch (s->tpm_version) {
        case TPM_VERSION_2_0:
            s->tpm_msg->tag = cpu_to_be16(TPM2_ST_NO_SESSIONS);
            s->tpm_msg->len = cpu_to_be32(sizeof(struct tpm_hdr));
            s->tpm_msg->code = cpu_to_be32(TPM_RC_FAILURE);
            break;
        case TPM_VERSION_1_2:
            s->tpm_msg->tag = cpu_to_be16(TPM_TAG_RSP_COMMAND);
            s->tpm_msg->len = cpu_to_be32(sizeof(struct tpm_hdr));
            s->tpm_msg->code = cpu_to_be32(TPM_FAIL);
            break;
        default:
            g_debug("unsupport TPM version %u", s->tpm_version);
            g_assert_not_reached();
        }
        qio_channel_write(ioc, (char *)s->tpm_msg, be32_to_cpu(s->tpm_msg->len),
                          &error_abort);
    }

    qtest_remove_abrt_handler(ioc);
    g_free(s->tpm_msg);
    s->tpm_msg = NULL;
    object_unref(OBJECT(s->tpm_ioc));
    return NULL;
}

void *tpm_emu_ctrl_thread(void *data)
{
    TPMTestState *s = data;
    QIOChannelSocket *lioc = qio_channel_socket_new();
    QIOChannel *ioc;

    qio_channel_socket_listen_sync(lioc, s->addr, 1, &error_abort);

    g_mutex_lock(&s->data_mutex);
    s->data_cond_signal = true;
    g_mutex_unlock(&s->data_mutex);
    g_cond_signal(&s->data_cond);

    qio_channel_wait(QIO_CHANNEL(lioc), G_IO_IN);
    ioc = QIO_CHANNEL(qio_channel_socket_accept(lioc, &error_abort));
    g_assert(ioc);
    qtest_add_abrt_handler(tpm_emu_close_ioc, ioc);

    {
        uint32_t cmd = 0;
        struct iovec iov = { .iov_base = &cmd, .iov_len = sizeof(cmd) };
        int *pfd = NULL;
        size_t nfd = 0;

        qio_channel_readv_full(ioc, &iov, 1, &pfd, &nfd, 0, &error_abort);
        cmd = be32_to_cpu(cmd);
        g_assert_cmpint(cmd, ==, CMD_SET_DATAFD);
        g_assert_cmpint(nfd, ==, 1);
        s->tpm_ioc = QIO_CHANNEL(qio_channel_socket_new_fd(*pfd, &error_abort));
        g_free(pfd);

        cmd = 0;
        qio_channel_write(ioc, (char *)&cmd, sizeof(cmd), &error_abort);

        s->emu_tpm_thread = g_thread_new(NULL, tpm_emu_tpm_thread, s);
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
            /* the tpm data thread is expected to finish now */
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
        case CMD_GET_TPMESTABLISHED: {
            ptm_est est = {
                .u.resp.bit = 0,
            };
            qio_channel_write(ioc, (char *)&est, sizeof(est), &error_abort);
            break;
        }
        default:
            g_debug("unimplemented %u", cmd);
            g_assert_not_reached();
        }
    }

    qtest_remove_abrt_handler(ioc);
    object_unref(OBJECT(ioc));
    object_unref(OBJECT(lioc));
    return NULL;
}

bool tpm_model_is_available(const char *args, const char *tpm_if)
{
    QTestState *qts;
    QDict *rsp_tpm;
    bool ret = false;

    qts = qtest_init(args);
    if (!qts) {
        return false;
    }

    rsp_tpm = qtest_qmp(qts, "{ 'execute': 'query-tpm'}");
    if (!qdict_haskey(rsp_tpm, "error")) {
        QDict *rsp_models = qtest_qmp(qts,
                                      "{ 'execute': 'query-tpm-models'}");
        if (qdict_haskey(rsp_models, "return")) {
            QList *models = qdict_get_qlist(rsp_models, "return");
            QListEntry *e;

            QLIST_FOREACH_ENTRY(models, e) {
                QString *s = qobject_to(QString, qlist_entry_obj(e));
                const char *ename = qstring_get_str(s);
                if (!strcmp(ename, tpm_if)) {
                    ret = true;
                    break;
                }
            }
        }
        qobject_unref(rsp_models);
    }
    qobject_unref(rsp_tpm);
    qtest_quit(qts);

    return ret;
}
