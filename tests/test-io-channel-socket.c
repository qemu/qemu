/*
 * QEMU I/O channel sockets test
 *
 * Copyright (c) 2015-2016 Red Hat, Inc.
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

#include "qemu/osdep.h"
#include "io/channel-socket.h"
#include "io/channel-util.h"
#include "io-channel-helpers.h"
#include "qapi/error.h"

#ifndef AI_ADDRCONFIG
# define AI_ADDRCONFIG 0
#endif
#ifndef EAI_ADDRFAMILY
# define EAI_ADDRFAMILY 0
#endif

static int check_bind(const char *hostname, bool *has_proto)
{
    int fd = -1;
    struct addrinfo ai, *res = NULL;
    int rc;
    int ret = -1;

    memset(&ai, 0, sizeof(ai));
    ai.ai_flags = AI_CANONNAME | AI_ADDRCONFIG;
    ai.ai_family = AF_UNSPEC;
    ai.ai_socktype = SOCK_STREAM;

    /* lookup */
    rc = getaddrinfo(hostname, NULL, &ai, &res);
    if (rc != 0) {
        if (rc == EAI_ADDRFAMILY ||
            rc == EAI_FAMILY) {
            *has_proto = false;
            goto done;
        }
        goto cleanup;
    }

    fd = qemu_socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        goto cleanup;
    }

    if (bind(fd, res->ai_addr, res->ai_addrlen) < 0) {
        if (errno == EADDRNOTAVAIL) {
            *has_proto = false;
            goto done;
        }
        goto cleanup;
    }

    *has_proto = true;
 done:
    ret = 0;

 cleanup:
    if (fd != -1) {
        close(fd);
    }
    if (res) {
        freeaddrinfo(res);
    }
    return ret;
}

static int check_protocol_support(bool *has_ipv4, bool *has_ipv6)
{
    if (check_bind("127.0.0.1", has_ipv4) < 0) {
        return -1;
    }
    if (check_bind("::1", has_ipv6) < 0) {
        return -1;
    }

    return 0;
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

        g_free(connect_addr->u.inet.data->port);
        connect_addr->u.inet.data->port = g_strdup(laddr->u.inet.data->port);

        qapi_free_SocketAddress(laddr);
    }

    *src = QIO_CHANNEL(qio_channel_socket_new());
    qio_channel_socket_connect_sync(
        QIO_CHANNEL_SOCKET(*src), connect_addr, &error_abort);
    qio_channel_set_delay(*src, false);

    qio_channel_wait(QIO_CHANNEL(lioc), G_IO_IN);
    *dst = QIO_CHANNEL(qio_channel_socket_accept(lioc, &error_abort));
    g_assert(*dst);

    test_io_channel_set_socket_bufs(*src, *dst);

    object_unref(OBJECT(lioc));
}


struct TestIOChannelData {
    bool err;
    GMainLoop *loop;
};


static void test_io_channel_complete(QIOTask *task,
                                     gpointer opaque)
{
    struct TestIOChannelData *data = opaque;
    data->err = qio_task_propagate_error(task, NULL);
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

        g_free(connect_addr->u.inet.data->port);
        connect_addr->u.inet.data->port = g_strdup(laddr->u.inet.data->port);

        qapi_free_SocketAddress(laddr);
    }

    *src = QIO_CHANNEL(qio_channel_socket_new());

    qio_channel_socket_connect_async(
        QIO_CHANNEL_SOCKET(*src), connect_addr,
        test_io_channel_complete, &data, NULL);

    g_main_loop_run(data.loop);
    g_main_context_iteration(g_main_context_default(), FALSE);

    g_assert(!data.err);

    qio_channel_wait(QIO_CHANNEL(lioc), G_IO_IN);
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
        g_assert(qio_channel_has_feature(src, QIO_CHANNEL_FEATURE_SHUTDOWN));
        g_assert(qio_channel_has_feature(dst, QIO_CHANNEL_FEATURE_SHUTDOWN));

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
        g_assert(qio_channel_has_feature(src, QIO_CHANNEL_FEATURE_SHUTDOWN));
        g_assert(qio_channel_has_feature(dst, QIO_CHANNEL_FEATURE_SHUTDOWN));

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
        g_assert(qio_channel_has_feature(src, QIO_CHANNEL_FEATURE_SHUTDOWN));
        g_assert(qio_channel_has_feature(dst, QIO_CHANNEL_FEATURE_SHUTDOWN));

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
        g_assert(qio_channel_has_feature(src, QIO_CHANNEL_FEATURE_SHUTDOWN));
        g_assert(qio_channel_has_feature(dst, QIO_CHANNEL_FEATURE_SHUTDOWN));

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
    listen_addr->u.inet.data = g_new(InetSocketAddress, 1);
    *listen_addr->u.inet.data = (InetSocketAddress) {
        .host = g_strdup("127.0.0.1"),
        .port = NULL, /* Auto-select */
    };

    connect_addr->type = SOCKET_ADDRESS_KIND_INET;
    connect_addr->u.inet.data = g_new(InetSocketAddress, 1);
    *connect_addr->u.inet.data = (InetSocketAddress) {
        .host = g_strdup("127.0.0.1"),
        .port = NULL, /* Filled in later */
    };

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
    listen_addr->u.inet.data = g_new(InetSocketAddress, 1);
    *listen_addr->u.inet.data = (InetSocketAddress) {
        .host = g_strdup("::1"),
        .port = NULL, /* Auto-select */
    };

    connect_addr->type = SOCKET_ADDRESS_KIND_INET;
    connect_addr->u.inet.data = g_new(InetSocketAddress, 1);
    *connect_addr->u.inet.data = (InetSocketAddress) {
        .host = g_strdup("::1"),
        .port = NULL, /* Filled in later */
    };

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
    listen_addr->u.q_unix.data = g_new0(UnixSocketAddress, 1);
    listen_addr->u.q_unix.data->path = g_strdup(TEST_SOCKET);

    connect_addr->type = SOCKET_ADDRESS_KIND_UNIX;
    connect_addr->u.q_unix.data = g_new0(UnixSocketAddress, 1);
    connect_addr->u.q_unix.data->path = g_strdup(TEST_SOCKET);

    test_io_channel(async, listen_addr, connect_addr, true);

    qapi_free_SocketAddress(listen_addr);
    qapi_free_SocketAddress(connect_addr);
    g_assert(g_file_test(TEST_SOCKET, G_FILE_TEST_EXISTS) == FALSE);
}


static void test_io_channel_unix_sync(void)
{
    return test_io_channel_unix(false);
}


static void test_io_channel_unix_async(void)
{
    return test_io_channel_unix(true);
}

static void test_io_channel_unix_fd_pass(void)
{
    SocketAddress *listen_addr = g_new0(SocketAddress, 1);
    SocketAddress *connect_addr = g_new0(SocketAddress, 1);
    QIOChannel *src, *dst;
    int testfd;
    int fdsend[3];
    int *fdrecv = NULL;
    size_t nfdrecv = 0;
    size_t i;
    char bufsend[12], bufrecv[12];
    struct iovec iosend[1], iorecv[1];

#define TEST_SOCKET "test-io-channel-socket.sock"
#define TEST_FILE "test-io-channel-socket.txt"

    testfd = open(TEST_FILE, O_RDWR|O_TRUNC|O_CREAT, 0700);
    g_assert(testfd != -1);
    fdsend[0] = testfd;
    fdsend[1] = testfd;
    fdsend[2] = testfd;

    listen_addr->type = SOCKET_ADDRESS_KIND_UNIX;
    listen_addr->u.q_unix.data = g_new0(UnixSocketAddress, 1);
    listen_addr->u.q_unix.data->path = g_strdup(TEST_SOCKET);

    connect_addr->type = SOCKET_ADDRESS_KIND_UNIX;
    connect_addr->u.q_unix.data = g_new0(UnixSocketAddress, 1);
    connect_addr->u.q_unix.data->path = g_strdup(TEST_SOCKET);

    test_io_channel_setup_sync(listen_addr, connect_addr, &src, &dst);

    memcpy(bufsend, "Hello World", G_N_ELEMENTS(bufsend));

    iosend[0].iov_base = bufsend;
    iosend[0].iov_len = G_N_ELEMENTS(bufsend);

    iorecv[0].iov_base = bufrecv;
    iorecv[0].iov_len = G_N_ELEMENTS(bufrecv);

    g_assert(qio_channel_has_feature(src, QIO_CHANNEL_FEATURE_FD_PASS));
    g_assert(qio_channel_has_feature(dst, QIO_CHANNEL_FEATURE_FD_PASS));

    qio_channel_writev_full(src,
                            iosend,
                            G_N_ELEMENTS(iosend),
                            fdsend,
                            G_N_ELEMENTS(fdsend),
                            &error_abort);

    qio_channel_readv_full(dst,
                           iorecv,
                           G_N_ELEMENTS(iorecv),
                           &fdrecv,
                           &nfdrecv,
                           &error_abort);

    g_assert(nfdrecv == G_N_ELEMENTS(fdsend));
    /* Each recvd FD should be different from sent FD */
    for (i = 0; i < nfdrecv; i++) {
        g_assert_cmpint(fdrecv[i], !=, testfd);
    }
    /* Each recvd FD should be different from each other */
    g_assert_cmpint(fdrecv[0], !=, fdrecv[1]);
    g_assert_cmpint(fdrecv[0], !=, fdrecv[2]);
    g_assert_cmpint(fdrecv[1], !=, fdrecv[2]);

    /* Check the I/O buf we sent at the same time matches */
    g_assert(memcmp(bufsend, bufrecv, G_N_ELEMENTS(bufsend)) == 0);

    /* Write some data into the FD we received */
    g_assert(write(fdrecv[0], bufsend, G_N_ELEMENTS(bufsend)) ==
             G_N_ELEMENTS(bufsend));

    /* Read data from the original FD and make sure it matches */
    memset(bufrecv, 0, G_N_ELEMENTS(bufrecv));
    g_assert(lseek(testfd, 0, SEEK_SET) == 0);
    g_assert(read(testfd, bufrecv, G_N_ELEMENTS(bufrecv)) ==
             G_N_ELEMENTS(bufrecv));
    g_assert(memcmp(bufsend, bufrecv, G_N_ELEMENTS(bufsend)) == 0);

    object_unref(OBJECT(src));
    object_unref(OBJECT(dst));
    qapi_free_SocketAddress(listen_addr);
    qapi_free_SocketAddress(connect_addr);
    unlink(TEST_SOCKET);
    unlink(TEST_FILE);
    close(testfd);
    for (i = 0; i < nfdrecv; i++) {
        close(fdrecv[i]);
    }
    g_free(fdrecv);
}

static void test_io_channel_unix_listen_cleanup(void)
{
    QIOChannelSocket *ioc;
    struct sockaddr_un un;
    int sock;

#define TEST_SOCKET "test-io-channel-socket.sock"

    ioc = qio_channel_socket_new();

    /* Manually bind ioc without calling the qio api to avoid setting
     * the LISTEN feature */
    sock = qemu_socket(PF_UNIX, SOCK_STREAM, 0);
    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    snprintf(un.sun_path, sizeof(un.sun_path), "%s", TEST_SOCKET);
    unlink(TEST_SOCKET);
    bind(sock, (struct sockaddr *)&un, sizeof(un));
    ioc->fd = sock;
    ioc->localAddrLen = sizeof(ioc->localAddr);
    getsockname(sock, (struct sockaddr *)&ioc->localAddr,
                &ioc->localAddrLen);

    g_assert(g_file_test(TEST_SOCKET, G_FILE_TEST_EXISTS));
    object_unref(OBJECT(ioc));
    g_assert(g_file_test(TEST_SOCKET, G_FILE_TEST_EXISTS));

    unlink(TEST_SOCKET);
}

#endif /* _WIN32 */


static void test_io_channel_ipv4_fd(void)
{
    QIOChannel *ioc;
    int fd = -1;
    struct sockaddr_in sa = {
        .sin_family = AF_INET,
        .sin_addr = {
            .s_addr =  htonl(INADDR_LOOPBACK),
        }
        /* Leave port unset for auto-assign */
    };
    socklen_t salen = sizeof(sa);

    fd = socket(AF_INET, SOCK_STREAM, 0);
    g_assert_cmpint(fd, >, -1);

    g_assert_cmpint(bind(fd, (struct sockaddr *)&sa, salen), ==, 0);

    ioc = qio_channel_new_fd(fd, &error_abort);

    g_assert_cmpstr(object_get_typename(OBJECT(ioc)),
                    ==,
                    TYPE_QIO_CHANNEL_SOCKET);

    object_unref(OBJECT(ioc));
}


int main(int argc, char **argv)
{
    bool has_ipv4, has_ipv6;

    module_call_init(MODULE_INIT_QOM);
    socket_init();

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
        g_test_add_func("/io/channel/socket/ipv4-fd",
                        test_io_channel_ipv4_fd);
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
    g_test_add_func("/io/channel/socket/unix-fd-pass",
                    test_io_channel_unix_fd_pass);
    g_test_add_func("/io/channel/socket/unix-listen-cleanup",
                    test_io_channel_unix_listen_cleanup);
#endif /* _WIN32 */

    return g_test_run();
}
