/*
 * QTest testcase for netdev stream and dgram
 *
 * Copyright (c) 2022 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/sockets.h"
#include <glib/gstdio.h>
#include "../unit/socket-helpers.h"
#include "libqtest.h"
#include "qapi/qmp/qstring.h"
#include "qemu/sockets.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qapi-visit-sockets.h"

#define CONNECTION_TIMEOUT    60

#define EXPECT_STATE(q, e, t)                             \
do {                                                      \
    char *resp = NULL;                                    \
    g_test_timer_start();                                 \
    do {                                                  \
        g_free(resp);                                     \
        resp = qtest_hmp(q, "info network");              \
        if (t) {                                          \
            strrchr(resp, t)[0] = 0;                      \
        }                                                 \
        if (g_str_equal(resp, e)) {                       \
            break;                                        \
        }                                                 \
    } while (g_test_timer_elapsed() < CONNECTION_TIMEOUT); \
    g_assert_cmpstr(resp, ==, e);                         \
    g_free(resp);                                         \
} while (0)

static gchar *tmpdir;

static int inet_get_free_port_socket_ipv4(int sock)
{
    struct sockaddr_in addr;
    socklen_t len;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0;
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        return -1;
    }

    len = sizeof(addr);
    if (getsockname(sock,  (struct sockaddr *)&addr, &len) < 0) {
        return -1;
    }

    return ntohs(addr.sin_port);
}

static int inet_get_free_port_socket_ipv6(int sock)
{
    struct sockaddr_in6 addr;
    socklen_t len;

    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;
    addr.sin6_port = 0;
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        return -1;
    }

    len = sizeof(addr);
    if (getsockname(sock,  (struct sockaddr *)&addr, &len) < 0) {
        return -1;
    }

    return ntohs(addr.sin6_port);
}

static int inet_get_free_port_multiple(int nb, int *port, bool ipv6)
{
    int sock[nb];
    int i;

    for (i = 0; i < nb; i++) {
        sock[i] = socket(ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
        if (sock[i] < 0) {
            break;
        }
        port[i] = ipv6 ? inet_get_free_port_socket_ipv6(sock[i]) :
                         inet_get_free_port_socket_ipv4(sock[i]);
        if (port[i] == -1) {
            break;
        }
    }

    nb = i;
    for (i = 0; i < nb; i++) {
        closesocket(sock[i]);
    }

    return nb;
}

static int inet_get_free_port(bool ipv6)
{
    int nb, port;

    nb = inet_get_free_port_multiple(1, &port, ipv6);
    g_assert_cmpint(nb, ==, 1);

    return port;
}

static void test_stream_inet_ipv4(void)
{
    QTestState *qts0, *qts1;
    char *expect;
    int port;

    port = inet_get_free_port(false);
    qts0 = qtest_initf("-nodefaults -M none "
                       "-netdev stream,id=st0,server=true,addr.type=inet,"
                       "addr.ipv4=on,addr.ipv6=off,"
                       "addr.host=127.0.0.1,addr.port=%d", port);

    EXPECT_STATE(qts0, "st0: index=0,type=stream,\r\n", 0);

    qts1 = qtest_initf("-nodefaults -M none "
                       "-netdev stream,server=false,id=st0,addr.type=inet,"
                       "addr.ipv4=on,addr.ipv6=off,"
                       "addr.host=127.0.0.1,addr.port=%d", port);

    expect = g_strdup_printf("st0: index=0,type=stream,tcp:127.0.0.1:%d\r\n",
                             port);
    EXPECT_STATE(qts1, expect, 0);
    g_free(expect);

    /* the port is unknown, check only the address */
    EXPECT_STATE(qts0, "st0: index=0,type=stream,tcp:127.0.0.1", ':');

    qtest_quit(qts1);
    qtest_quit(qts0);
}

static void wait_stream_connected(QTestState *qts, const char *id,
                                  SocketAddress **addr)
{
    QDict *resp, *data;
    QString *qstr;
    QObject *obj;
    Visitor *v = NULL;

    resp = qtest_qmp_eventwait_ref(qts, "NETDEV_STREAM_CONNECTED");
    g_assert_nonnull(resp);
    data = qdict_get_qdict(resp, "data");
    g_assert_nonnull(data);

    qstr = qobject_to(QString, qdict_get(data, "netdev-id"));
    g_assert_nonnull(data);

    g_assert(!strcmp(qstring_get_str(qstr), id));

    obj = qdict_get(data, "addr");

    v = qobject_input_visitor_new(obj);
    visit_type_SocketAddress(v, NULL, addr, NULL);
    visit_free(v);
    qobject_unref(resp);
}

static void wait_stream_disconnected(QTestState *qts, const char *id)
{
    QDict *resp, *data;
    QString *qstr;

    resp = qtest_qmp_eventwait_ref(qts, "NETDEV_STREAM_DISCONNECTED");
    g_assert_nonnull(resp);
    data = qdict_get_qdict(resp, "data");
    g_assert_nonnull(data);

    qstr = qobject_to(QString, qdict_get(data, "netdev-id"));
    g_assert_nonnull(data);

    g_assert(!strcmp(qstring_get_str(qstr), id));
    qobject_unref(resp);
}

static void test_stream_inet_reconnect(void)
{
    QTestState *qts0, *qts1;
    int port;
    SocketAddress *addr;

    port = inet_get_free_port(false);
    qts0 = qtest_initf("-nodefaults -M none "
                       "-netdev stream,id=st0,server=true,addr.type=inet,"
                       "addr.ipv4=on,addr.ipv6=off,"
                       "addr.host=127.0.0.1,addr.port=%d", port);

    EXPECT_STATE(qts0, "st0: index=0,type=stream,\r\n", 0);

    qts1 = qtest_initf("-nodefaults -M none "
                       "-netdev stream,server=false,id=st0,addr.type=inet,"
                       "addr.ipv4=on,addr.ipv6=off,reconnect=1,"
                       "addr.host=127.0.0.1,addr.port=%d", port);

    wait_stream_connected(qts0, "st0", &addr);
    g_assert_cmpint(addr->type, ==, SOCKET_ADDRESS_TYPE_INET);
    g_assert_cmpstr(addr->u.inet.host, ==, "127.0.0.1");
    qapi_free_SocketAddress(addr);

    /* kill server */
    qtest_quit(qts0);

    /* check client has been disconnected */
    wait_stream_disconnected(qts1, "st0");

    /* restart server */
    qts0 = qtest_initf("-nodefaults -M none "
                       "-netdev stream,id=st0,server=true,addr.type=inet,"
                       "addr.ipv4=on,addr.ipv6=off,"
                       "addr.host=127.0.0.1,addr.port=%d", port);

    /* wait connection events*/
    wait_stream_connected(qts0, "st0", &addr);
    g_assert_cmpint(addr->type, ==, SOCKET_ADDRESS_TYPE_INET);
    g_assert_cmpstr(addr->u.inet.host, ==, "127.0.0.1");
    qapi_free_SocketAddress(addr);

    wait_stream_connected(qts1, "st0", &addr);
    g_assert_cmpint(addr->type, ==, SOCKET_ADDRESS_TYPE_INET);
    g_assert_cmpstr(addr->u.inet.host, ==, "127.0.0.1");
    g_assert_cmpint(atoi(addr->u.inet.port), ==, port);
    qapi_free_SocketAddress(addr);

    qtest_quit(qts1);
    qtest_quit(qts0);
}

static void test_stream_inet_ipv6(void)
{
    QTestState *qts0, *qts1;
    char *expect;
    int port;

    port = inet_get_free_port(true);
    qts0 = qtest_initf("-nodefaults -M none "
                       "-netdev stream,id=st0,server=true,addr.type=inet,"
                       "addr.ipv4=off,addr.ipv6=on,"
                       "addr.host=::1,addr.port=%d", port);

    EXPECT_STATE(qts0, "st0: index=0,type=stream,\r\n", 0);

    qts1 = qtest_initf("-nodefaults -M none "
                       "-netdev stream,server=false,id=st0,addr.type=inet,"
                       "addr.ipv4=off,addr.ipv6=on,"
                       "addr.host=::1,addr.port=%d", port);

    expect = g_strdup_printf("st0: index=0,type=stream,tcp:::1:%d\r\n",
                             port);
    EXPECT_STATE(qts1, expect, 0);
    g_free(expect);

    /* the port is unknown, check only the address */
    EXPECT_STATE(qts0, "st0: index=0,type=stream,tcp:::1", ':');

    qtest_quit(qts1);
    qtest_quit(qts0);
}

static void test_stream_unix(void)
{
    QTestState *qts0, *qts1;
    char *expect;
    gchar *path;

    path = g_strconcat(tmpdir, "/stream_unix", NULL);

    qts0 = qtest_initf("-nodefaults -M none "
                       "-netdev stream,id=st0,server=true,"
                       "addr.type=unix,addr.path=%s,",
                       path);

    EXPECT_STATE(qts0, "st0: index=0,type=stream,\r\n", 0);

    qts1 = qtest_initf("-nodefaults -M none "
                       "-netdev stream,id=st0,server=false,"
                       "addr.type=unix,addr.path=%s",
                       path);

    expect = g_strdup_printf("st0: index=0,type=stream,unix:%s\r\n", path);
    EXPECT_STATE(qts1, expect, 0);
    EXPECT_STATE(qts0, expect, 0);
    g_free(expect);
    g_free(path);

    qtest_quit(qts1);
    qtest_quit(qts0);
}

#ifdef CONFIG_LINUX
static void test_stream_unix_abstract(void)
{
    QTestState *qts0, *qts1;
    char *expect;
    gchar *path;

    path = g_strconcat(tmpdir, "/stream_unix_abstract", NULL);

    qts0 = qtest_initf("-nodefaults -M none "
                       "-netdev stream,id=st0,server=true,"
                       "addr.type=unix,addr.path=%s,"
                       "addr.abstract=on",
                       path);

    EXPECT_STATE(qts0, "st0: index=0,type=stream,\r\n", 0);

    qts1 = qtest_initf("-nodefaults -M none "
                       "-netdev stream,id=st0,server=false,"
                       "addr.type=unix,addr.path=%s,addr.abstract=on",
                       path);

    expect = g_strdup_printf("st0: index=0,type=stream,unix:%s\r\n", path);
    EXPECT_STATE(qts1, expect, 0);
    EXPECT_STATE(qts0, expect, 0);
    g_free(expect);
    g_free(path);

    qtest_quit(qts1);
    qtest_quit(qts0);
}
#endif

#ifndef _WIN32
static void test_stream_fd(void)
{
    QTestState *qts0, *qts1;
    int sock[2];
    int ret;

    ret = socketpair(AF_LOCAL, SOCK_STREAM, 0, sock);
    g_assert_true(ret == 0);

    qts0 = qtest_initf("-nodefaults -M none "
                       "-netdev stream,id=st0,addr.type=fd,addr.str=%d",
                       sock[0]);

    EXPECT_STATE(qts0, "st0: index=0,type=stream,unix:\r\n", 0);

    qts1 = qtest_initf("-nodefaults -M none "
                       "-netdev stream,id=st0,addr.type=fd,addr.str=%d",
                       sock[1]);

    EXPECT_STATE(qts1, "st0: index=0,type=stream,unix:\r\n", 0);
    EXPECT_STATE(qts0, "st0: index=0,type=stream,unix:\r\n", 0);

    qtest_quit(qts1);
    qtest_quit(qts0);

    closesocket(sock[0]);
    closesocket(sock[1]);
}
#endif

static void test_dgram_inet(void)
{
    QTestState *qts0, *qts1;
    char *expect;
    int port[2];
    int nb;

    nb = inet_get_free_port_multiple(2, port, false);
    g_assert_cmpint(nb, ==, 2);

    qts0 = qtest_initf("-nodefaults -M none "
                       "-netdev dgram,id=st0,"
                       "local.type=inet,local.host=127.0.0.1,local.port=%d,"
                       "remote.type=inet,remote.host=127.0.0.1,remote.port=%d",
                        port[0], port[1]);

    expect = g_strdup_printf("st0: index=0,type=dgram,"
                             "udp=127.0.0.1:%d/127.0.0.1:%d\r\n",
                             port[0], port[1]);
    EXPECT_STATE(qts0, expect, 0);
    g_free(expect);

    qts1 = qtest_initf("-nodefaults -M none "
                       "-netdev dgram,id=st0,"
                       "local.type=inet,local.host=127.0.0.1,local.port=%d,"
                       "remote.type=inet,remote.host=127.0.0.1,remote.port=%d",
                        port[1], port[0]);

    expect = g_strdup_printf("st0: index=0,type=dgram,"
                             "udp=127.0.0.1:%d/127.0.0.1:%d\r\n",
                             port[1], port[0]);
    EXPECT_STATE(qts1, expect, 0);
    g_free(expect);

    qtest_quit(qts1);
    qtest_quit(qts0);
}

#ifndef _WIN32
static void test_dgram_mcast(void)
{
    QTestState *qts;

    qts = qtest_initf("-nodefaults -M none "
                      "-netdev dgram,id=st0,"
                      "remote.type=inet,remote.host=230.0.0.1,remote.port=1234");

    EXPECT_STATE(qts, "st0: index=0,type=dgram,mcast=230.0.0.1:1234\r\n", 0);

    qtest_quit(qts);
}

static void test_dgram_unix(void)
{
    QTestState *qts0, *qts1;
    char *expect;
    gchar *path0, *path1;

    path0 = g_strconcat(tmpdir, "/dgram_unix0", NULL);
    path1 = g_strconcat(tmpdir, "/dgram_unix1", NULL);

    qts0 = qtest_initf("-nodefaults -M none "
                       "-netdev dgram,id=st0,local.type=unix,local.path=%s,"
                       "remote.type=unix,remote.path=%s",
                       path0, path1);

    expect = g_strdup_printf("st0: index=0,type=dgram,udp=%s:%s\r\n",
                             path0, path1);
    EXPECT_STATE(qts0, expect, 0);
    g_free(expect);

    qts1 = qtest_initf("-nodefaults -M none "
                       "-netdev dgram,id=st0,local.type=unix,local.path=%s,"
                       "remote.type=unix,remote.path=%s",
                       path1, path0);


    expect = g_strdup_printf("st0: index=0,type=dgram,udp=%s:%s\r\n",
                             path1, path0);
    EXPECT_STATE(qts1, expect, 0);
    g_free(expect);

    unlink(path0);
    g_free(path0);
    unlink(path1);
    g_free(path1);

    qtest_quit(qts1);
    qtest_quit(qts0);
}

static void test_dgram_fd(void)
{
    QTestState *qts0, *qts1;
    char *expect;
    int ret;
    int sv[2];

    ret = socketpair(PF_UNIX, SOCK_DGRAM, 0, sv);
    g_assert_cmpint(ret, !=, -1);

    qts0 = qtest_initf("-nodefaults -M none "
                       "-netdev dgram,id=st0,local.type=fd,local.str=%d",
                       sv[0]);

    expect = g_strdup_printf("st0: index=0,type=dgram,fd=%d unix\r\n", sv[0]);
    EXPECT_STATE(qts0, expect, 0);
    g_free(expect);

    qts1 = qtest_initf("-nodefaults -M none "
                       "-netdev dgram,id=st0,local.type=fd,local.str=%d",
                       sv[1]);


    expect = g_strdup_printf("st0: index=0,type=dgram,fd=%d unix\r\n", sv[1]);
    EXPECT_STATE(qts1, expect, 0);
    g_free(expect);

    qtest_quit(qts1);
    qtest_quit(qts0);

    closesocket(sv[0]);
    closesocket(sv[1]);
}
#endif

int main(int argc, char **argv)
{
    int ret;
    bool has_ipv4, has_ipv6, has_afunix;
    g_autoptr(GError) err = NULL;

    socket_init();
    g_test_init(&argc, &argv, NULL);

    if (socket_check_protocol_support(&has_ipv4, &has_ipv6) < 0) {
        g_error("socket_check_protocol_support() failed\n");
    }

    tmpdir = g_dir_make_tmp("netdev-socket.XXXXXX", &err);
    if (tmpdir == NULL) {
        g_error("Can't create temporary directory in %s: %s",
                g_get_tmp_dir(), err->message);
    }

    if (has_ipv4) {
        qtest_add_func("/netdev/stream/inet/ipv4", test_stream_inet_ipv4);
        qtest_add_func("/netdev/dgram/inet", test_dgram_inet);
#ifndef _WIN32
        qtest_add_func("/netdev/dgram/mcast", test_dgram_mcast);
#endif
        qtest_add_func("/netdev/stream/inet/reconnect",
                       test_stream_inet_reconnect);
    }
    if (has_ipv6) {
        qtest_add_func("/netdev/stream/inet/ipv6", test_stream_inet_ipv6);
    }

    socket_check_afunix_support(&has_afunix);
    if (has_afunix) {
#ifndef _WIN32
        qtest_add_func("/netdev/dgram/unix", test_dgram_unix);
#endif
        qtest_add_func("/netdev/stream/unix", test_stream_unix);
#ifdef CONFIG_LINUX
        qtest_add_func("/netdev/stream/unix/abstract",
                       test_stream_unix_abstract);
#endif
#ifndef _WIN32
        qtest_add_func("/netdev/stream/fd", test_stream_fd);
        qtest_add_func("/netdev/dgram/fd", test_dgram_fd);
#endif
    }

    ret = g_test_run();

    g_rmdir(tmpdir);
    g_free(tmpdir);

    return ret;
}
