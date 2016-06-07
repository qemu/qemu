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
#include "qemu/iov.h"
#include "qemu/sockets.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"

static void test_redirector_tx(void)
{
#ifndef _WIN32
/* socketpair(PF_UNIX) which does not exist on windows */

    int backend_sock[2], recv_sock;
    char *cmdline;
    uint32_t ret = 0, len = 0;
    char send_buf[] = "Hello!!";
    char sock_path0[] = "filter-redirector0.XXXXXX";
    char sock_path1[] = "filter-redirector1.XXXXXX";
    char *recv_buf;
    uint32_t size = sizeof(send_buf);
    size = htonl(size);

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, backend_sock);
    g_assert_cmpint(ret, !=, -1);

    ret = mkstemp(sock_path0);
    g_assert_cmpint(ret, !=, -1);
    ret = mkstemp(sock_path1);
    g_assert_cmpint(ret, !=, -1);

    cmdline = g_strdup_printf("-netdev socket,id=qtest-bn0,fd=%d "
                "-device rtl8139,netdev=qtest-bn0,id=qtest-e0 "
                "-chardev socket,id=redirector0,path=%s,server,nowait "
                "-chardev socket,id=redirector1,path=%s,server,nowait "
                "-chardev socket,id=redirector2,path=%s,nowait "
                "-object filter-redirector,id=qtest-f0,netdev=qtest-bn0,"
                "queue=tx,outdev=redirector0 "
                "-object filter-redirector,id=qtest-f1,netdev=qtest-bn0,"
                "queue=tx,indev=redirector2 "
                "-object filter-redirector,id=qtest-f2,netdev=qtest-bn0,"
                "queue=tx,outdev=redirector1 "
                , backend_sock[1], sock_path0, sock_path1, sock_path0);
    qtest_start(cmdline);
    g_free(cmdline);

    recv_sock = unix_connect(sock_path1, NULL);
    g_assert_cmpint(recv_sock, !=, -1);

    /* send a qmp command to guarantee that 'connected' is setting to true. */
    qmp("{ 'execute' : 'query-status'}");

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

    ret = qemu_recv(recv_sock, &len, sizeof(len), 0);
    g_assert_cmpint(ret, ==, sizeof(len));
    len = ntohl(len);

    g_assert_cmpint(len, ==, sizeof(send_buf));
    recv_buf = g_malloc(len);
    ret = qemu_recv(recv_sock, recv_buf, len, 0);
    g_assert_cmpstr(recv_buf, ==, send_buf);

    g_free(recv_buf);
    close(recv_sock);
    unlink(sock_path0);
    unlink(sock_path1);
    qtest_end();

#endif
}

static void test_redirector_rx(void)
{
#ifndef _WIN32
/* socketpair(PF_UNIX) which does not exist on windows */

    int backend_sock[2], send_sock;
    char *cmdline;
    uint32_t ret = 0, len = 0;
    char send_buf[] = "Hello!!";
    char sock_path0[] = "filter-redirector0.XXXXXX";
    char sock_path1[] = "filter-redirector1.XXXXXX";
    char *recv_buf;
    uint32_t size = sizeof(send_buf);
    size = htonl(size);

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, backend_sock);
    g_assert_cmpint(ret, !=, -1);

    ret = mkstemp(sock_path0);
    g_assert_cmpint(ret, !=, -1);
    ret = mkstemp(sock_path1);
    g_assert_cmpint(ret, !=, -1);

    cmdline = g_strdup_printf("-netdev socket,id=qtest-bn0,fd=%d "
                "-device rtl8139,netdev=qtest-bn0,id=qtest-e0 "
                "-chardev socket,id=redirector0,path=%s,server,nowait "
                "-chardev socket,id=redirector1,path=%s,server,nowait "
                "-chardev socket,id=redirector2,path=%s,nowait "
                "-object filter-redirector,id=qtest-f0,netdev=qtest-bn0,"
                "queue=rx,indev=redirector0 "
                "-object filter-redirector,id=qtest-f1,netdev=qtest-bn0,"
                "queue=rx,outdev=redirector2 "
                "-object filter-redirector,id=qtest-f2,netdev=qtest-bn0,"
                "queue=rx,indev=redirector1 "
                , backend_sock[1], sock_path0, sock_path1, sock_path0);
    qtest_start(cmdline);
    g_free(cmdline);

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
    qmp("{ 'execute' : 'query-status'}");

    ret = iov_send(send_sock, iov, 2, 0, sizeof(size) + sizeof(send_buf));
    g_assert_cmpint(ret, ==, sizeof(send_buf) + sizeof(size));
    close(send_sock);

    ret = qemu_recv(backend_sock[0], &len, sizeof(len), 0);
    g_assert_cmpint(ret, ==, sizeof(len));
    len = ntohl(len);

    g_assert_cmpint(len, ==, sizeof(send_buf));
    recv_buf = g_malloc(len);
    ret = qemu_recv(backend_sock[0], recv_buf, len, 0);
    g_assert_cmpstr(recv_buf, ==, send_buf);

    g_free(recv_buf);
    unlink(sock_path0);
    unlink(sock_path1);
    qtest_end();

#endif
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/netfilter/redirector_tx", test_redirector_tx);
    qtest_add_func("/netfilter/redirector_rx", test_redirector_rx);
    ret = g_test_run();

    return ret;
}
