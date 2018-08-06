/*
 * QTest testcase for filter-mirror
 *
 * Copyright (c) 2016 FUJITSU LIMITED
 * Author: Zhang Chen <zhangchen.fnst@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qapi/qmp/qdict.h"
#include "qemu/iov.h"
#include "qemu/sockets.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"

/* TODO actually test the results and get rid of this */
#define qmp_discard_response(...) qobject_unref(qmp(__VA_ARGS__))

static void test_mirror(void)
{
    int send_sock[2], recv_sock;
    uint32_t ret = 0, len = 0;
    char send_buf[] = "Hello! filter-mirror~";
    char sock_path[] = "filter-mirror.XXXXXX";
    char *recv_buf;
    uint32_t size = sizeof(send_buf);
    size = htonl(size);
    const char *devstr = "e1000";

    if (g_str_equal(qtest_get_arch(), "s390x")) {
        devstr = "virtio-net-ccw";
    }

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, send_sock);
    g_assert_cmpint(ret, !=, -1);

    ret = mkstemp(sock_path);
    g_assert_cmpint(ret, !=, -1);

    global_qtest = qtest_initf(
        "-netdev socket,id=qtest-bn0,fd=%d "
        "-device %s,netdev=qtest-bn0,id=qtest-e0 "
        "-chardev socket,id=mirror0,path=%s,server,nowait "
        "-object filter-mirror,id=qtest-f0,netdev=qtest-bn0,queue=tx,outdev=mirror0 "
        , send_sock[1], devstr, sock_path);

    recv_sock = unix_connect(sock_path, NULL);
    g_assert_cmpint(recv_sock, !=, -1);

    struct iovec iov[] = {
        {
            .iov_base = &size,
            .iov_len = sizeof(size),
        }, {
            .iov_base = send_buf,
            .iov_len = sizeof(send_buf),
        },
    };

    /* send a qmp command to guarantee that 'connected' is setting to true. */
    qmp_discard_response("{ 'execute' : 'query-status'}");
    ret = iov_send(send_sock[0], iov, 2, 0, sizeof(size) + sizeof(send_buf));
    g_assert_cmpint(ret, ==, sizeof(send_buf) + sizeof(size));
    close(send_sock[0]);

    ret = qemu_recv(recv_sock, &len, sizeof(len), 0);
    g_assert_cmpint(ret, ==, sizeof(len));
    len = ntohl(len);

    g_assert_cmpint(len, ==, sizeof(send_buf));
    recv_buf = g_malloc(len);
    ret = qemu_recv(recv_sock, recv_buf, len, 0);
    g_assert_cmpstr(recv_buf, ==, send_buf);

    g_free(recv_buf);
    close(recv_sock);
    unlink(sock_path);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/netfilter/mirror", test_mirror);
    ret = g_test_run();
    qtest_end();

    return ret;
}
