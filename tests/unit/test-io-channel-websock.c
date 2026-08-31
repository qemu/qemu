/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QEMU I/O channel websock test
 *
 * Copyright (c) 2026 Virtuozzo International GmbH
 */

#include "qemu/osdep.h"
#include "io/channel-websock.h"
#include "io/channel-socket.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/sockets.h"
#include "qom/object.h"

#define TYPE_QIO_CHANNEL_STALL "qio-channel-stall"
OBJECT_DECLARE_SIMPLE_TYPE(QIOChannelStall, QIO_CHANNEL_STALL)

/*
 * Reports QIO_CHANNEL_ERR_BLOCK for the first @rstalls reads and @wstalls
 * writes, the way a TLS channel does when a record arrives split across TCP
 * segments or the socket cannot take the whole reply at once.
 */
struct QIOChannelStall {
    QIOChannel parent;
    QIOChannel *master;
    unsigned rstalls;
    unsigned wstalls;
};

static ssize_t qio_channel_stall_readv(QIOChannel *ioc,
                                       const struct iovec *iov,
                                       size_t niov,
                                       int **fds,
                                       size_t *nfds,
                                       int flags,
                                       Error **errp)
{
    QIOChannelStall *sioc = QIO_CHANNEL_STALL(ioc);

    if (sioc->rstalls) {
        sioc->rstalls--;
        return QIO_CHANNEL_ERR_BLOCK;
    }
    return qio_channel_readv_full(sioc->master, iov, niov, fds, nfds,
                                  flags, errp);
}

static ssize_t qio_channel_stall_writev(QIOChannel *ioc,
                                        const struct iovec *iov,
                                        size_t niov,
                                        int *fds,
                                        size_t nfds,
                                        int flags,
                                        Error **errp)
{
    QIOChannelStall *sioc = QIO_CHANNEL_STALL(ioc);

    if (sioc->wstalls) {
        sioc->wstalls--;
        return QIO_CHANNEL_ERR_BLOCK;
    }
    return qio_channel_writev_full(sioc->master, iov, niov, fds, nfds,
                                   flags, errp);
}

static int qio_channel_stall_set_blocking(QIOChannel *ioc, bool enabled,
                                          Error **errp)
{
    QIOChannelStall *sioc = QIO_CHANNEL_STALL(ioc);

    return qio_channel_set_blocking(sioc->master, enabled, errp) ? 0 : -1;
}

static int qio_channel_stall_close(QIOChannel *ioc, Error **errp)
{
    QIOChannelStall *sioc = QIO_CHANNEL_STALL(ioc);

    return qio_channel_close(sioc->master, errp);
}

static GSource *qio_channel_stall_create_watch(QIOChannel *ioc,
                                               GIOCondition condition)
{
    QIOChannelStall *sioc = QIO_CHANNEL_STALL(ioc);

    return qio_channel_create_watch(sioc->master, condition);
}

static void qio_channel_stall_finalize(Object *obj)
{
    QIOChannelStall *sioc = QIO_CHANNEL_STALL(obj);

    object_unref(OBJECT(sioc->master));
}

static void qio_channel_stall_class_init(ObjectClass *klass,
                                         const void *class_data G_GNUC_UNUSED)
{
    QIOChannelClass *ioc_klass = QIO_CHANNEL_CLASS(klass);

    ioc_klass->io_writev = qio_channel_stall_writev;
    ioc_klass->io_readv = qio_channel_stall_readv;
    ioc_klass->io_set_blocking = qio_channel_stall_set_blocking;
    ioc_klass->io_close = qio_channel_stall_close;
    ioc_klass->io_create_watch = qio_channel_stall_create_watch;
}

static const TypeInfo qio_channel_stall_info = {
    .parent = TYPE_QIO_CHANNEL,
    .name = TYPE_QIO_CHANNEL_STALL,
    .instance_size = sizeof(QIOChannelStall),
    .instance_finalize = qio_channel_stall_finalize,
    .class_init = qio_channel_stall_class_init,
};

static QIOChannelStall *qio_channel_stall_new(QIOChannel *master,
                                              unsigned rstalls,
                                              unsigned wstalls)
{
    QIOChannelStall *sioc = QIO_CHANNEL_STALL(
        object_new(TYPE_QIO_CHANNEL_STALL));

    object_ref(OBJECT(master));
    sioc->master = master;
    sioc->rstalls = rstalls;
    sioc->wstalls = wstalls;

    return sioc;
}

typedef struct {
    bool finished;
    bool failed;
} QIOChannelWebsockHandshake;

static void test_websock_handshake_done(QIOTask *task, gpointer opaque)
{
    QIOChannelWebsockHandshake *res = opaque;

    res->finished = true;
    res->failed = qio_task_propagate_error(task, NULL);
}

/*
 * Drives a server-side handshake against @request and returns whatever
 * the server wrote back, NUL terminated. The handshake is expected to
 * fail; the point of the test is the HTTP response that goes with it.
 */
static char *test_websock_handshake_reply(const char *request,
                                          unsigned rstalls, unsigned wstalls)
{
    QIOChannelWebsockHandshake res = { false, false };
    QIOChannelSocket *cli, *srv;
    QIOChannelStall *stall;
    QIOChannelWebsock *wioc;
    GMainContext *mainloop;
    int channel[2];
    char *reply;
    ssize_t got;

    g_assert(qemu_socketpair(AF_UNIX, SOCK_STREAM, 0, channel) == 0);

    cli = qio_channel_socket_new_fd(channel[0], &error_abort);
    srv = qio_channel_socket_new_fd(channel[1], &error_abort);
    qio_channel_set_blocking(QIO_CHANNEL(srv), false, &error_abort);
    qio_channel_set_blocking(QIO_CHANNEL(cli), false, &error_abort);

    stall = qio_channel_stall_new(QIO_CHANNEL(srv), rstalls, wstalls);
    wioc = qio_channel_websock_new_server(QIO_CHANNEL(stall));
    qio_channel_websock_handshake(wioc, test_websock_handshake_done,
                                  &res, NULL);

    qio_channel_write_all(QIO_CHANNEL(cli), request, strlen(request),
                          &error_abort);

    mainloop = g_main_context_default();
    while (!res.finished) {
        g_main_context_iteration(mainloop, TRUE);
    }
    g_assert(res.failed);

    reply = g_malloc0(1024);
    got = qio_channel_read(QIO_CHANNEL(cli), reply, 1023, &error_abort);
    if (got > 0) {
        reply[got] = '\0';
    }

    object_unref(OBJECT(wioc));
    object_unref(OBJECT(stall));
    object_unref(OBJECT(srv));
    object_unref(OBJECT(cli));

    return reply;
}

static void test_websock_bad_request(const void *opaque)
{
    const char *request = opaque;
    g_autofree char *reply = test_websock_handshake_reply(request, 0, 0);

    g_assert_true(g_str_has_prefix(reply, "HTTP/1.1 400 Bad Request\r\n"));
}

static void test_websock_stalled_read(const void *opaque)
{
    const char *request = opaque;
    g_autofree char *reply = test_websock_handshake_reply(request, 1, 0);

    g_assert_true(g_str_has_prefix(reply, "HTTP/1.1 400 Bad Request\r\n"));
}

static void test_websock_stalled_write(const void *opaque)
{
    const char *request = opaque;
    g_autofree char *reply = test_websock_handshake_reply(request, 0, 1);

    g_assert_true(g_str_has_prefix(reply, "HTTP/1.1 400 Bad Request\r\n"));
}

int main(int argc, char **argv)
{
    module_call_init(MODULE_INIT_QOM);
    type_register_static(&qio_channel_stall_info);
    g_test_init(&argc, &argv, NULL);

#define TEST_BAD_REQUEST(name, request)                         \
    g_test_add_data_func("/io/channel/websock/bad-request/" name, \
                         request, test_websock_bad_request)

    /*
     * A greeting with no space at all used to leave the response buffer
     * empty, which drove the handshake into a zero length write.
     */
    TEST_BAD_REQUEST("no-space", "stats\r\nx\r\n\r\n");
    TEST_BAD_REQUEST("method-only", "GET\r\nx\r\n\r\n");
    TEST_BAD_REQUEST("no-version", "GET /\r\nx\r\n\r\n");
    TEST_BAD_REQUEST("bad-method", "POST / HTTP/1.1\r\nx: y\r\n\r\n");
    TEST_BAD_REQUEST("bad-version", "GET / HTTP/1.0\r\nx: y\r\n\r\n");

    /* A read which blocks before any header arrives is not a fatal error. */
    g_test_add_data_func("/io/channel/websock/stalled-read",
                         "stats\r\nx\r\n\r\n", test_websock_stalled_read);
    g_test_add_data_func("/io/channel/websock/stalled-write",
                         "stats\r\nx\r\n\r\n", test_websock_stalled_write);

    return g_test_run();
}
