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
static char *test_websock_handshake_reply(const char *request)
{
    QIOChannelWebsockHandshake res = { false, false };
    QIOChannelSocket *cli, *srv;
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

    wioc = qio_channel_websock_new_server(QIO_CHANNEL(srv));
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
    object_unref(OBJECT(srv));
    object_unref(OBJECT(cli));

    return reply;
}

static void test_websock_bad_request(const void *opaque)
{
    const char *request = opaque;
    g_autofree char *reply = test_websock_handshake_reply(request);

    g_assert_true(g_str_has_prefix(reply, "HTTP/1.1 400 Bad Request\r\n"));
}

int main(int argc, char **argv)
{
    module_call_init(MODULE_INIT_QOM);
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

    return g_test_run();
}
