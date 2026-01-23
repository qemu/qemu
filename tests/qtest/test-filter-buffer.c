/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QTest testcase for filter-buffer
 *
 * Copyright (c) 2025 Red Hat, Inc.
 * Author: Jason Wang <jasowang@redhat.com>
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qobject/qdict.h"
#include "qemu/iov.h"
#include "qemu/sockets.h"

/*
 * Test that changing interval at runtime affects packet release timing.
 *
 * Traffic flow with filter-buffer and filter-redirector:
 *
 * test side                        | qemu side
 *                                  |
 * +--------+                       | +---------+
 * |  send  +------------------------>| backend |
 * | sock[0]|                       | +----+----+
 * +--------+                       |      |
 *                                  | +----v----+
 *                                  | |  fbuf0  | filter-buffer (queue=tx)
 *                                  | +----+----+
 *                                  |      |
 *                                  | +----v----+  +----------+
 *                                  | |   rd0   +->| chardev0 |
 *                                  | +---------+  +----+-----+
 *                                  |                   |
 * +--------+                       |                   |
 * |  recv  |<--------------------------------------+
 * |  sock  |                       |
 * +--------+                       |
 *
 * The test verifies that when interval is changed via qom-set, the timer
 * is rescheduled immediately, causing buffered packets to be released
 * at the new interval rather than waiting for the old interval to elapse.
 */
static void test_change_interval_timer(void)
{
    QTestState *qts;
    QDict *response;
    int backend_sock[2], recv_sock;
    int ret;
    char send_buf[] = "Hello filter-buffer!";
    char recv_buf[128];
    char sock_path[] = "filter-buffer-test.XXXXXX";
    uint32_t size = sizeof(send_buf);
    uint32_t len;

    size = htonl(size);

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, backend_sock);
    g_assert_cmpint(ret, !=, -1);

    ret = mkstemp(sock_path);
    g_assert_cmpint(ret, !=, -1);

    /*
     * Start QEMU with:
     * - socket backend connected to our socketpair
     * - filter-buffer with a very long interval (1000 seconds)
     * - filter-redirector to send released packets to a chardev socket
     *
     * queue=tx intercepts packets going from backend to the guest,
     * i.e., data we send from the test side.
     */
    qts = qtest_initf(
        "-nic socket,id=qtest-bn0,fd=%d "
        "-chardev socket,id=chardev0,path=%s,server=on,wait=off "
        "-object filter-buffer,id=fbuf0,netdev=qtest-bn0,"
        "queue=tx,interval=1000000000 "
        "-object filter-redirector,id=rd0,netdev=qtest-bn0,"
        "queue=tx,outdev=chardev0",
        backend_sock[1], sock_path);

    /* Connect to the chardev socket to receive redirected packets */
    recv_sock = unix_connect(sock_path, NULL);
    g_assert_cmpint(recv_sock, !=, -1);

    /* Send a QMP command to ensure chardev connection is established */
    qtest_qmp_assert_success(qts, "{ 'execute' : 'query-status'}");

    /*
     * Send a packet from the test side.
     * It should be buffered by filter-buffer.
     */
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

    /*
     * Advance virtual clock by 1 second (1,000,000,000 ns).
     * This is much less than the 1000 second interval, so the packet
     * should still be buffered.
     */
    qtest_clock_step(qts, 1000000000LL);

    /* Try to receive with non-blocking - should fail (packet still buffered) */
    ret = recv(recv_sock, recv_buf, sizeof(recv_buf), MSG_DONTWAIT);
    g_assert_cmpint(ret, ==, -1);
    g_assert(errno == EAGAIN || errno == EWOULDBLOCK);

    /*
     * Now change the interval to 1000 us (1ms) via qom-set.
     * This should reschedule the timer to fire in 1ms from now.
     */
    response = qtest_qmp(qts,
                         "{'execute': 'qom-set',"
                         " 'arguments': {"
                         "   'path': 'fbuf0',"
                         "   'property': 'interval',"
                         "   'value': 1000"
                         "}}");
    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    qobject_unref(response);

    /*
     * Advance virtual clock by 2ms (2,000,000 ns).
     * This exceeds the new 1ms interval, so the timer should fire
     * and release the buffered packet.
     *
     * If the interval change didn't take effect immediately, we would
     * still be waiting for the original 1000 second interval to elapse,
     * and the packet would not be released.
     */
    qtest_clock_step(qts, 2000000LL);

    /*
     * Now we should be able to receive the packet through the redirector.
     * The packet was released by filter-buffer and sent to filter-redirector,
     * which forwarded it to the chardev socket.
     */
    ret = recv(recv_sock, &len, sizeof(len), 0);
    g_assert_cmpint(ret, ==, sizeof(len));
    len = ntohl(len);
    g_assert_cmpint(len, ==, sizeof(send_buf));

    ret = recv(recv_sock, recv_buf, len, 0);
    g_assert_cmpint(ret, ==, len);
    g_assert_cmpstr(recv_buf, ==, send_buf);

    close(recv_sock);
    close(backend_sock[0]);
    unlink(sock_path);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/netfilter/change_interval_timer",
                   test_change_interval_timer);
    return g_test_run();
}
