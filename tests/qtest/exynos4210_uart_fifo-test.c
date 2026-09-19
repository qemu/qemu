/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "libqtest.h"

/* defs from exynos4210.c */
#define UART0_BASE_ADDR     0x13800000
#define UART0_FIFO_SIZE     256
#define UFCON      0x0008 /* FIFO Control             */
#define UTRSTAT    0x0010 /* Tx/Rx Status             */
#define URXH       0x0024 /* Receive Buffer           */
#define UFCON_FIFO_ENABLE                    0x1
#define UTRSTAT_Rx_BUFFER_DATA_READY         0x1

#define TEST_BUFFER_LEN        ((4 * UART0_FIFO_SIZE))

static bool uart_wait_for_flag(QTestState *qts, uint32_t event_addr,
                               uint32_t flag)
{
    while (true) {
        if ((qtest_readl(qts, event_addr) & flag)) {
            return true;
        }
        g_usleep(1000);
    }

    return false;
}

static void uart_receive_string(QTestState *qts, int sock_fd, const char *in,
                                char *out)
{
    size_t i, in_len = strlen(in);

    g_assert_true(send(sock_fd, in, in_len, 0) == in_len);
    for (i = 0; i < in_len; i++) {
        g_assert_true(uart_wait_for_flag(qts,
            UART0_BASE_ADDR + UTRSTAT, UTRSTAT_Rx_BUFFER_DATA_READY));
        out[i] = qtest_readl(qts, UART0_BASE_ADDR + URXH);
    }
    out[i] = '\0';
}

static void enable_uart_fifo(QTestState *qts)
{
    qtest_writel(qts, UART0_BASE_ADDR + UFCON, UFCON_FIFO_ENABLE);
}

/*
 * If the FIFO overflows when receiving data from the chardev, early input
 * chunks will be lost, and this will timeout waiting to read the full
 * buffer. If the FIFO behaves correctly, we will receive the full buffer.
 */
static void test_recv_large_str(void)
{
    int sock_fd;
    char *sendbuf, *recvbuf;
    QTestState *qts = qtest_init_with_serial("-M smdkc210", &sock_fd);

    enable_uart_fifo(qts);

    sendbuf = g_new0(char, TEST_BUFFER_LEN);
    recvbuf = g_new0(char, TEST_BUFFER_LEN);
    memset(sendbuf, 'A', TEST_BUFFER_LEN);
    sendbuf[TEST_BUFFER_LEN - 1] = '\x00';

    g_assert_true(0 == strlen(recvbuf));
    g_assert_true(TEST_BUFFER_LEN - 1 == strlen(sendbuf));

    uart_receive_string(qts, sock_fd, sendbuf, recvbuf);

    g_assert_true(TEST_BUFFER_LEN - 1 == strlen(recvbuf));
    g_assert_true(strcmp(sendbuf, recvbuf) == 0);

    close(sock_fd);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    qtest_add_func("exynos4210/uart/receive_large_str", test_recv_large_str);
    return g_test_run();
}
