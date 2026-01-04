/*
 * QTest testcase for filter-redirector
 *
 * Copyright (c) 2016 FUJITSU LIMITED
 * Author: Zhang Chen <zhangchen.fnst@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 *
 * Case 1, tx traffic flow:
 *
 * qemu side              | test side
 *                        |
 * +---------+            |  +-------+
 * | backend <---------------+ sock0 |
 * +----+----+            |  +-------+
 *      |                 |
 * +----v----+  +-------+ |
 * |  rd0    +->+chardev| |
 * +---------+  +---+---+ |
 *                  |     |
 * +---------+      |     |
 * |  rd1    <------+     |
 * +----+----+            |
 *      |                 |
 * +----v----+            |  +-------+
 * |  rd2    +--------------->sock1  |
 * +---------+            |  +-------+
 *                        +
 *
 * --------------------------------------
 * Case 2, rx traffic flow
 * qemu side              | test side
 *                        |
 * +---------+            |  +-------+
 * | backend +---------------> sock1 |
 * +----^----+            |  +-------+
 *      |                 |
 * +----+----+  +-------+ |
 * |  rd0    +<-+chardev| |
 * +---------+  +---+---+ |
 *                  ^     |
 * +---------+      |     |
 * |  rd1    +------+     |
 * +----^----+            |
 *      |                 |
 * +----+----+            |  +-------+
 * |  rd2    <---------------+sock0  |
 * +---------+            |  +-------+
 *                        +
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qobject/qdict.h"
#include "qemu/iov.h"
#include "qemu/sockets.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"

static void test_redirector_tx(void)
{
    int backend_sock[2], recv_sock;
    uint32_t ret = 0, len = 0;
    char send_buf[] = "Hello!!";
    char sock_path0[] = "filter-redirector0.XXXXXX";
    char sock_path1[] = "filter-redirector1.XXXXXX";
    char *recv_buf;
    uint32_t size = sizeof(send_buf);
    size = htonl(size);
    QTestState *qts;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, backend_sock);
    g_assert_cmpint(ret, !=, -1);

    ret = mkstemp(sock_path0);
    g_assert_cmpint(ret, !=, -1);
    ret = mkstemp(sock_path1);
    g_assert_cmpint(ret, !=, -1);

    qts = qtest_initf(
        "-nic socket,id=qtest-bn0,fd=%d "
        "-chardev socket,id=redirector0,path=%s,server=on,wait=off "
        "-chardev socket,id=redirector1,path=%s,server=on,wait=off "
        "-chardev socket,id=redirector2,path=%s "
        "-object filter-redirector,id=qtest-f0,netdev=qtest-bn0,"
        "queue=tx,outdev=redirector0 "
        "-object filter-redirector,id=qtest-f1,netdev=qtest-bn0,"
        "queue=tx,indev=redirector2 "
        "-object filter-redirector,id=qtest-f2,netdev=qtest-bn0,"
        "queue=tx,outdev=redirector1 ", backend_sock[1],
        sock_path0, sock_path1, sock_path0);

    recv_sock = unix_connect(sock_path1, NULL);
    g_assert_cmpint(recv_sock, !=, -1);

    /* send a qmp command to guarantee that 'connected' is setting to true. */
    qtest_qmp_assert_success(qts, "{ 'execute' : 'query-status'}");

    struct iovec iov[] = {
        {
            .iov_base = &size,
            .iov_len = sizeof(size),
        }, {
            .iov_base = send_buf,
            .iov_len = sizeof(send_buf),
        },
    };

    ret = iov_send(backend_sock[0], iov, 2, 0, sizeof(size) + sizeof(send_buf));
    g_assert_cmpint(ret, ==, sizeof(send_buf) + sizeof(size));
    close(backend_sock[0]);

    ret = recv(recv_sock, &len, sizeof(len), 0);
    g_assert_cmpint(ret, ==, sizeof(len));
    len = ntohl(len);

    g_assert_cmpint(len, ==, sizeof(send_buf));
    recv_buf = g_malloc(len);
    ret = recv(recv_sock, recv_buf, len, 0);
    g_assert_cmpint(ret, ==, len);
    g_assert_cmpstr(recv_buf, ==, send_buf);

    g_free(recv_buf);
    close(recv_sock);
    unlink(sock_path0);
    unlink(sock_path1);
    qtest_quit(qts);
}

static void test_redirector_rx(void)
{
    int backend_sock[2], send_sock;
    uint32_t ret = 0, len = 0;
    char send_buf[] = "Hello!!";
    char sock_path0[] = "filter-redirector0.XXXXXX";
    char sock_path1[] = "filter-redirector1.XXXXXX";
    char *recv_buf;
    uint32_t size = sizeof(send_buf);
    size = htonl(size);
    QTestState *qts;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, backend_sock);
    g_assert_cmpint(ret, !=, -1);

    ret = mkstemp(sock_path0);
    g_assert_cmpint(ret, !=, -1);
    ret = mkstemp(sock_path1);
    g_assert_cmpint(ret, !=, -1);

    qts = qtest_initf(
        "-nic socket,id=qtest-bn0,fd=%d "
        "-chardev socket,id=redirector0,path=%s,server=on,wait=off "
        "-chardev socket,id=redirector1,path=%s,server=on,wait=off "
        "-chardev socket,id=redirector2,path=%s "
        "-object filter-redirector,id=qtest-f0,netdev=qtest-bn0,"
        "queue=rx,indev=redirector0 "
        "-object filter-redirector,id=qtest-f1,netdev=qtest-bn0,"
        "queue=rx,outdev=redirector2 "
        "-object filter-redirector,id=qtest-f2,netdev=qtest-bn0,"
        "queue=rx,indev=redirector1 ", backend_sock[1],
        sock_path0, sock_path1, sock_path0);

    struct iovec iov[] = {
        {
            .iov_base = &size,
            .iov_len = sizeof(size),
        }, {
            .iov_base = send_buf,
            .iov_len = sizeof(send_buf),
        },
    };

    send_sock = unix_connect(sock_path1, NULL);
    g_assert_cmpint(send_sock, !=, -1);
    /* send a qmp command to guarantee that 'connected' is setting to true. */
    qtest_qmp_assert_success(qts, "{ 'execute' : 'query-status'}");

    ret = iov_send(send_sock, iov, 2, 0, sizeof(size) + sizeof(send_buf));
    g_assert_cmpint(ret, ==, sizeof(send_buf) + sizeof(size));

    ret = recv(backend_sock[0], &len, sizeof(len), 0);
    g_assert_cmpint(ret, ==, sizeof(len));
    len = ntohl(len);

    g_assert_cmpint(len, ==, sizeof(send_buf));
    recv_buf = g_malloc(len);
    ret = recv(backend_sock[0], recv_buf, len, 0);
    g_assert_cmpint(ret, ==, len);
    g_assert_cmpstr(recv_buf, ==, send_buf);

    close(send_sock);
    g_free(recv_buf);
    unlink(sock_path0);
    unlink(sock_path1);
    qtest_quit(qts);
}

/*
 * Test filter-redirector status on/off switching.
 *
 * This test verifies that:
 * 1. When status is set to "off", the filter stops receiving data from indev
 * 2. When status is set back to "on", the filter resumes receiving data
 */
static void test_redirector_status(void)
{
    int backend_sock[2], send_sock;
    uint32_t ret = 0, len = 0;
    char send_buf[] = "Hello!!";
    char sock_path0[] = "filter-redirector0.XXXXXX";
    char *recv_buf;
    uint32_t size = sizeof(send_buf);
    size = htonl(size);
    QTestState *qts;
    struct timeval tv;
    fd_set rfds;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, backend_sock);
    g_assert_cmpint(ret, !=, -1);

    ret = mkstemp(sock_path0);
    g_assert_cmpint(ret, !=, -1);

    /*
     * Setup a simple rx path:
     * chardev (sock_path0) -> filter-redirector -> socket backend
     */
    qts = qtest_initf(
        "-nic socket,id=qtest-bn0,fd=%d "
        "-chardev socket,id=redirector0,path=%s,server=on,wait=off "
        "-object filter-redirector,id=qtest-f0,netdev=qtest-bn0,"
        "queue=rx,indev=redirector0 ",
        backend_sock[1], sock_path0);

    send_sock = unix_connect(sock_path0, NULL);
    g_assert_cmpint(send_sock, !=, -1);

    /* send a qmp command to guarantee that 'connected' is setting to true. */
    qtest_qmp_assert_success(qts, "{ 'execute' : 'query-status'}");

    struct iovec iov[] = {
        {
            .iov_base = &size,
            .iov_len = sizeof(size),
        }, {
            .iov_base = send_buf,
            .iov_len = sizeof(send_buf),
        },
    };

    /*
     * Test 1: Set status to "off" and verify data is not received
     */
    qtest_qmp_assert_success(qts,
        "{ 'execute': 'qom-set', 'arguments': "
        "{ 'path': '/objects/qtest-f0', 'property': 'status', 'value': 'off' }}");

    ret = iov_send(send_sock, iov, 2, 0, sizeof(size) + sizeof(send_buf));
    g_assert_cmpint(ret, ==, sizeof(send_buf) + sizeof(size));

    /*
     * Use select with timeout to check if data arrives.
     * When status is off, no data should arrive.
     */
    FD_ZERO(&rfds);
    FD_SET(backend_sock[0], &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 500000;  /* 500ms timeout */
    ret = select(backend_sock[0] + 1, &rfds, NULL, NULL, &tv);
    g_assert_cmpint(ret, ==, 0);  /* Should timeout, no data */

    /*
     * Test 2: Set status back to "on" and verify data is received
     */
    qtest_qmp_assert_success(qts,
        "{ 'execute': 'qom-set', 'arguments': "
        "{ 'path': '/objects/qtest-f0', 'property': 'status', 'value': 'on' }}");

    ret = iov_send(send_sock, iov, 2, 0, sizeof(size) + sizeof(send_buf));
    g_assert_cmpint(ret, ==, sizeof(send_buf) + sizeof(size));

    ret = recv(backend_sock[0], &len, sizeof(len), 0);
    g_assert_cmpint(ret, ==, sizeof(len));
    len = ntohl(len);

    g_assert_cmpint(len, ==, sizeof(send_buf));
    recv_buf = g_malloc(len);
    ret = recv(backend_sock[0], recv_buf, len, 0);
    g_assert_cmpint(ret, ==, len);
    g_assert_cmpstr(recv_buf, ==, send_buf);

    g_free(recv_buf);
    close(send_sock);
    unlink(sock_path0);
    qtest_quit(qts);
}

/*
 * Test filter-redirector created with status=off.
 *
 * This test verifies that when a filter-redirector is created with
 * status=off, it does not receive data until status is set to on.
 */
static void test_redirector_init_status_off(void)
{
    int backend_sock[2], send_sock;
    uint32_t ret = 0, len = 0;
    char send_buf[] = "Hello!!";
    char sock_path0[] = "filter-redirector0.XXXXXX";
    char *recv_buf;
    uint32_t size = sizeof(send_buf);
    size = htonl(size);
    QTestState *qts;
    struct timeval tv;
    fd_set rfds;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, backend_sock);
    g_assert_cmpint(ret, !=, -1);

    ret = mkstemp(sock_path0);
    g_assert_cmpint(ret, !=, -1);

    /*
     * Create filter-redirector with status=off from the start
     */
    qts = qtest_initf(
        "-nic socket,id=qtest-bn0,fd=%d "
        "-chardev socket,id=redirector0,path=%s,server=on,wait=off "
        "-object filter-redirector,id=qtest-f0,netdev=qtest-bn0,"
        "queue=rx,indev=redirector0,status=off ",
        backend_sock[1], sock_path0);

    send_sock = unix_connect(sock_path0, NULL);
    g_assert_cmpint(send_sock, !=, -1);

    qtest_qmp_assert_success(qts, "{ 'execute' : 'query-status'}");

    struct iovec iov[] = {
        {
            .iov_base = &size,
            .iov_len = sizeof(size),
        }, {
            .iov_base = send_buf,
            .iov_len = sizeof(send_buf),
        },
    };

    /*
     * Test 1: Filter was created with status=off, data should not be received
     */
    ret = iov_send(send_sock, iov, 2, 0, sizeof(size) + sizeof(send_buf));
    g_assert_cmpint(ret, ==, sizeof(send_buf) + sizeof(size));

    FD_ZERO(&rfds);
    FD_SET(backend_sock[0], &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 500000;
    ret = select(backend_sock[0] + 1, &rfds, NULL, NULL, &tv);
    g_assert_cmpint(ret, ==, 0);  /* Should timeout, no data */

    /*
     * Test 2: Set status to "on" and verify data is received
     */
    qtest_qmp_assert_success(qts,
        "{ 'execute': 'qom-set', 'arguments': "
        "{ 'path': '/objects/qtest-f0', 'property': 'status', 'value': 'on' }}");

    ret = iov_send(send_sock, iov, 2, 0, sizeof(size) + sizeof(send_buf));
    g_assert_cmpint(ret, ==, sizeof(send_buf) + sizeof(size));

    ret = recv(backend_sock[0], &len, sizeof(len), 0);
    g_assert_cmpint(ret, ==, sizeof(len));
    len = ntohl(len);

    g_assert_cmpint(len, ==, sizeof(send_buf));
    recv_buf = g_malloc(len);
    ret = recv(backend_sock[0], recv_buf, len, 0);
    g_assert_cmpint(ret, ==, len);
    g_assert_cmpstr(recv_buf, ==, send_buf);

    g_free(recv_buf);
    close(send_sock);
    unlink(sock_path0);
    qtest_quit(qts);
}

static void test_redirector_rx_event_opened(void)
{
    int backend_sock[2], send_sock;
    uint32_t ret = 0, len = 0;
    char send_buf[] = "Hello!!";
    char send_buf2[] = "Hello2!!";
    char sock_path0[] = "filter-redirector0.XXXXXX";
    char *recv_buf;
    uint32_t size = sizeof(send_buf);
    uint32_t size2 = sizeof(send_buf2);
    size = htonl(size);
    size2 = htonl(size2);
    QTestState *qts;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, backend_sock);
    g_assert_cmpint(ret, !=, -1);

    ret = mkstemp(sock_path0);
    g_assert_cmpint(ret, !=, -1);

    qts = qtest_initf(
        "-nic socket,id=qtest-bn0,fd=%d "
        "-chardev socket,id=redirector0,path=%s,server=on,wait=off "
        "-object filter-redirector,id=qtest-f0,netdev=qtest-bn0,"
        "queue=rx,indev=redirector0 ",
        backend_sock[1], sock_path0);

    struct iovec iov[] = {
        {
            .iov_base = &size,
            .iov_len = sizeof(size),
        }, {
            .iov_base = send_buf,
            .iov_len = sizeof(send_buf),
        },
    };

    struct iovec iov2[] = {
        {
            .iov_base = &size2,
            .iov_len = sizeof(size2),
        }, {
            .iov_base = send_buf2,
            .iov_len = sizeof(send_buf2),
        },
    };

    /* First connection */
    send_sock = unix_connect(sock_path0, NULL);
    g_assert_cmpint(send_sock, !=, -1);
    qtest_qmp_assert_success(qts, "{ 'execute' : 'query-status'}");

    ret = iov_send(send_sock, iov, 2, 0, sizeof(size) + sizeof(send_buf));
    g_assert_cmpint(ret, ==, sizeof(send_buf) + sizeof(size));

    ret = recv(backend_sock[0], &len, sizeof(len), 0);
    g_assert_cmpint(ret, ==, sizeof(len));
    len = ntohl(len);
    g_assert_cmpint(len, ==, sizeof(send_buf));
    recv_buf = g_malloc(len);
    ret = recv(backend_sock[0], recv_buf, len, 0);
    g_assert_cmpint(ret, ==, len);
    g_assert_cmpstr(recv_buf, ==, send_buf);
    g_free(recv_buf);

    close(send_sock);

    /* Verify disconnection handling if needed, but mainly we want to test Reconnection */
    qtest_qmp_assert_success(qts, "{ 'execute' : 'query-status'}");

    /* Second connection */
    send_sock = unix_connect(sock_path0, NULL);
    g_assert_cmpint(send_sock, !=, -1);
    qtest_qmp_assert_success(qts, "{ 'execute' : 'query-status'}");

    ret = iov_send(send_sock, iov2, 2, 0, sizeof(size2) + sizeof(send_buf2));
    g_assert_cmpint(ret, ==, sizeof(send_buf2) + sizeof(size2));

    ret = recv(backend_sock[0], &len, sizeof(len), 0);
    g_assert_cmpint(ret, ==, sizeof(len));
    len = ntohl(len);
    g_assert_cmpint(len, ==, sizeof(send_buf2));
    recv_buf = g_malloc(len);
    ret = recv(backend_sock[0], recv_buf, len, 0);
    g_assert_cmpint(ret, ==, len);
    g_assert_cmpstr(recv_buf, ==, send_buf2);
    g_free(recv_buf);

    close(send_sock);
    unlink(sock_path0);
    qtest_quit(qts);
    close(backend_sock[0]);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/netfilter/redirector_tx", test_redirector_tx);
    qtest_add_func("/netfilter/redirector_rx", test_redirector_rx);
    qtest_add_func("/netfilter/redirector_status", test_redirector_status);
    qtest_add_func("/netfilter/redirector_init_status_off",
                   test_redirector_init_status_off);
    qtest_add_func("/netfilter/redirector_rx_event_opened",
                   test_redirector_rx_event_opened);
    return g_test_run();
}
