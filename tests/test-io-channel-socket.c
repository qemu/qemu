/*
 * QEMU I/O channel sockets test
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "io/channel-socket.h"
#include "io-channel-helpers.h"
#ifdef HAVE_IFADDRS_H
#include <ifaddrs.h>
#endif

static int check_protocol_support(bool *has_ipv4, bool *has_ipv6)
{
#ifdef HAVE_IFADDRS_H
    struct ifaddrs *ifaddr = NULL, *ifa;
    struct addrinfo hints = { 0 };
    struct addrinfo *ai = NULL;
    int gaierr;

    *has_ipv4 = *has_ipv6 = false;

    if (getifaddrs(&ifaddr) < 0) {
        g_printerr("Failed to lookup interface addresses: %s\n",
                   strerror(errno));
        return -1;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) {
            continue;
        }

        if (ifa->ifa_addr->sa_family == AF_INET) {
            *has_ipv4 = true;
        }
        if (ifa->ifa_addr->sa_family == AF_INET6) {
            *has_ipv6 = true;
        }
    }

    freeifaddrs(ifaddr);

    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;

    gaierr = getaddrinfo("::1", NULL, &hints, &ai);
    if (gaierr != 0) {
        if (gaierr == EAI_ADDRFAMILY ||
            gaierr == EAI_FAMILY ||
            gaierr == EAI_NONAME) {
            *has_ipv6 = false;
        } else {
            g_printerr("Failed to resolve ::1 address: %s\n",
                       gai_strerror(gaierr));
            return -1;
        }
    }

    freeaddrinfo(ai);

    return 0;
#else
    *has_ipv4 = *has_ipv6 = false;

    return -1;
#endif
}


static void test_io_channel_set_socket_bufs(QIOChannel *src,
                                            QIOChannel *dst)
{
    int buflen = 64 * 1024;

    /*
     * Make the socket buffers small so that we see
     * the effects of partial reads/writes
     */
    setsockopt(((QIOChannelSocket *)src)->fd,
               SOL_SOCKET, SO_SNDBUF,
               (char *)&buflen,
               sizeof(buflen));

    setsockopt(((QIOChannelSocket *)dst)->fd,
               SOL_SOCKET, SO_SNDBUF,
               (char *)&buflen,
               sizeof(buflen));
}


static void test_io_channel_setup_sync(SocketAddress *listen_addr,
                                       SocketAddress *connect_addr,
                                       QIOChannel **src,
                                       QIOChannel **dst)
{
    QIOChannelSocket *lioc;

    lioc = qio_channel_socket_new();
    qio_channel_socket_listen_sync(lioc, listen_addr, &error_abort);

    if (listen_addr->type == SOCKET_ADDRESS_KIND_INET) {
        SocketAddress *laddr = qio_channel_socket_get_local_address(
            lioc, &error_abort);

        g_free(connect_addr->u.inet->port);
        connect_addr->u.inet->port = g_strdup(laddr->u.inet->port);

        qapi_free_SocketAddress(laddr);
    }

    *src = QIO_CHANNEL(qio_channel_socket_new());
    qio_channel_socket_connect_sync(
        QIO_CHANNEL_SOCKET(*src), connect_addr, &error_abort);
    qio_channel_set_delay(*src, false);

    *dst = QIO_CHANNEL(qio_channel_socket_accept(lioc, &error_abort));
    g_assert(*dst);

    test_io_channel_set_socket_bufs(*src, *dst);

    object_unref(OBJECT(lioc));
}


struct TestIOChannelData {
    bool err;
    GMainLoop *loop;
};


static void test_io_channel_complete(Object *src,
                                     Error *err,
                                     gpointer opaque)
{
    struct TestIOChannelData *data = opaque;
    data->err = err != NULL;
    g_main_loop_quit(data->loop);
}


static void test_io_channel_setup_async(SocketAddress *listen_addr,
                                        SocketAddress *connect_addr,
                                        QIOChannel **src,
                                        QIOChannel **dst)
{
    QIOChannelSocket *lioc;
    struct TestIOChannelData data;

    data.loop = g_main_loop_new(g_main_context_default(),
                                TRUE);

    lioc = qio_channel_socket_new();
    qio_channel_socket_listen_async(
        lioc, listen_addr,
        test_io_channel_complete, &data, NULL);

    g_main_loop_run(data.loop);
    g_main_context_iteration(g_main_context_default(), FALSE);

    g_assert(!data.err);

    if (listen_addr->type == SOCKET_ADDRESS_KIND_INET) {
        SocketAddress *laddr = qio_channel_socket_get_local_address(
            lioc, &error_abort);

        g_free(connect_addr->u.inet->port);
        connect_addr->u.inet->port = g_strdup(laddr->u.inet->port);

        qapi_free_SocketAddress(laddr);
    }

    *src = QIO_CHANNEL(qio_channel_socket_new());

    qio_channel_socket_connect_async(
        QIO_CHANNEL_SOCKET(*src), connect_addr,
        test_io_channel_complete, &data, NULL);

    g_main_loop_run(data.loop);
    g_main_context_iteration(g_main_context_default(), FALSE);

    g_assert(!data.err);

    *dst = QIO_CHANNEL(qio_channel_socket_accept(lioc, &error_abort));
    g_assert(*dst);

    qio_channel_set_delay(*src, false);
    test_io_channel_set_socket_bufs(*src, *dst);

    object_unref(OBJECT(lioc));

    g_main_loop_unref(data.loop);
}


static void test_io_channel(bool async,
                            SocketAddress *listen_addr,
                            SocketAddress *connect_addr,
                            bool passFD)
{
    QIOChannel *src, *dst;
    QIOChannelTest *test;
    if (async) {
        test_io_channel_setup_async(listen_addr, connect_addr, &src, &dst);

        g_assert(!passFD ||
                 qio_channel_has_feature(src, QIO_CHANNEL_FEATURE_FD_PASS));
        g_assert(!passFD ||
                 qio_channel_has_feature(dst, QIO_CHANNEL_FEATURE_FD_PASS));

        test = qio_channel_test_new();
        qio_channel_test_run_threads(test, true, src, dst);
        qio_channel_test_validate(test);

        object_unref(OBJECT(src));
        object_unref(OBJECT(dst));

        test_io_channel_setup_async(listen_addr, connect_addr, &src, &dst);

        g_assert(!passFD ||
                 qio_channel_has_feature(src, QIO_CHANNEL_FEATURE_FD_PASS));
        g_assert(!passFD ||
                 qio_channel_has_feature(dst, QIO_CHANNEL_FEATURE_FD_PASS));

        test = qio_channel_test_new();
        qio_channel_test_run_threads(test, false, src, dst);
        qio_channel_test_validate(test);

        object_unref(OBJECT(src));
        object_unref(OBJECT(dst));
    } else {
        test_io_channel_setup_sync(listen_addr, connect_addr, &src, &dst);

        g_assert(!passFD ||
                 qio_channel_has_feature(src, QIO_CHANNEL_FEATURE_FD_PASS));
        g_assert(!passFD ||
                 qio_channel_has_feature(dst, QIO_CHANNEL_FEATURE_FD_PASS));

        test = qio_channel_test_new();
        qio_channel_test_run_threads(test, true, src, dst);
        qio_channel_test_validate(test);

        object_unref(OBJECT(src));
        object_unref(OBJECT(dst));

        test_io_channel_setup_sync(listen_addr, connect_addr, &src, &dst);

        g_assert(!passFD ||
                 qio_channel_has_feature(src, QIO_CHANNEL_FEATURE_FD_PASS));
        g_assert(!passFD ||
                 qio_channel_has_feature(dst, QIO_CHANNEL_FEATURE_FD_PASS));

        test = qio_channel_test_new();
        qio_channel_test_run_threads(test, false, src, dst);
        qio_channel_test_validate(test);

        object_unref(OBJECT(src));
        object_unref(OBJECT(dst));
    }
}


static void test_io_channel_ipv4(bool async)
{
    SocketAddress *listen_addr = g_new0(SocketAddress, 1);
    SocketAddress *connect_addr = g_new0(SocketAddress, 1);

    listen_addr->type = SOCKET_ADDRESS_KIND_INET;
    listen_addr->u.inet = g_new0(InetSocketAddress, 1);
    listen_addr->u.inet->host = g_strdup("127.0.0.1");
    listen_addr->u.inet->port = NULL; /* Auto-select */

    connect_addr->type = SOCKET_ADDRESS_KIND_INET;
    connect_addr->u.inet = g_new0(InetSocketAddress, 1);
    connect_addr->u.inet->host = g_strdup("127.0.0.1");
    connect_addr->u.inet->port = NULL; /* Filled in later */

    test_io_channel(async, listen_addr, connect_addr, false);

    qapi_free_SocketAddress(listen_addr);
    qapi_free_SocketAddress(connect_addr);
}


static void test_io_channel_ipv4_sync(void)
{
    return test_io_channel_ipv4(false);
}


static void test_io_channel_ipv4_async(void)
{
    return test_io_channel_ipv4(true);
}


static void test_io_channel_ipv6(bool async)
{
    SocketAddress *listen_addr = g_new0(SocketAddress, 1);
    SocketAddress *connect_addr = g_new0(SocketAddress, 1);

    listen_addr->type = SOCKET_ADDRESS_KIND_INET;
    listen_addr->u.inet = g_new0(InetSocketAddress, 1);
    listen_addr->u.inet->host = g_strdup("::1");
    listen_addr->u.inet->port = NULL; /* Auto-select */

    connect_addr->type = SOCKET_ADDRESS_KIND_INET;
    connect_addr->u.inet = g_new0(InetSocketAddress, 1);
    connect_addr->u.inet->host = g_strdup("::1");
    connect_addr->u.inet->port = NULL; /* Filled in later */

    test_io_channel(async, listen_addr, connect_addr, false);

    qapi_free_SocketAddress(listen_addr);
    qapi_free_SocketAddress(connect_addr);
}


static void test_io_channel_ipv6_sync(void)
{
    return test_io_channel_ipv6(false);
}


static void test_io_channel_ipv6_async(void)
{
    return test_io_channel_ipv6(true);
}


#ifndef _WIN32
static void test_io_channel_unix(bool async)
{
    SocketAddress *listen_addr = g_new0(SocketAddress, 1);
    SocketAddress *connect_addr = g_new0(SocketAddress, 1);

#define TEST_SOCKET "test-io-channel-socket.sock"
    listen_addr->type = SOCKET_ADDRESS_KIND_UNIX;
    listen_addr->u.q_unix = g_new0(UnixSocketAddress, 1);
    listen_addr->u.q_unix->path = g_strdup(TEST_SOCKET);

    connect_addr->type = SOCKET_ADDRESS_KIND_UNIX;
    connect_addr->u.q_unix = g_new0(UnixSocketAddress, 1);
    connect_addr->u.q_unix->path = g_strdup(TEST_SOCKET);

    test_io_channel(async, listen_addr, connect_addr, true);

    qapi_free_SocketAddress(listen_addr);
    qapi_free_SocketAddress(connect_addr);
    unlink(TEST_SOCKET);
}


static void test_io_channel_unix_sync(void)
{
    return test_io_channel_unix(false);
}


static void test_io_channel_unix_async(void)
{
    return test_io_channel_unix(true);
}
#endif /* _WIN32 */


int main(int argc, char **argv)
{
    bool has_ipv4, has_ipv6;

    module_call_init(MODULE_INIT_QOM);

    g_test_init(&argc, &argv, NULL);

    /* We're creating actual IPv4/6 sockets, so we should
     * check if the host running tests actually supports
     * each protocol to avoid breaking tests on machines
     * with either IPv4 or IPv6 disabled.
     */
    if (check_protocol_support(&has_ipv4, &has_ipv6) < 0) {
        return 1;
    }

    if (has_ipv4) {
        g_test_add_func("/io/channel/socket/ipv4-sync",
                        test_io_channel_ipv4_sync);
        g_test_add_func("/io/channel/socket/ipv4-async",
                        test_io_channel_ipv4_async);
    }
    if (has_ipv6) {
        g_test_add_func("/io/channel/socket/ipv6-sync",
                        test_io_channel_ipv6_sync);
        g_test_add_func("/io/channel/socket/ipv6-async",
                        test_io_channel_ipv6_async);
    }

#ifndef _WIN32
    g_test_add_func("/io/channel/socket/unix-sync",
                    test_io_channel_unix_sync);
    g_test_add_func("/io/channel/socket/unix-async",
                    test_io_channel_unix_async);
#endif /* _WIN32 */

    return g_test_run();
}
