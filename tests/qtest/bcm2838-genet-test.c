/*
 * QTest testcase for the BCM2838 GENET DMA rings
 *
 * Frames go out over a socket netdev. The producer and consumer indexes
 * of a ring count modulo 2^16.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"
#include "libqtest.h"

#define GENET_BASE          0xfd580000

#define RING_START_ADDR     0x14
#define RING_END_ADDR       0x1c

#define TDMA_DESC(i)        (0x4000 + (i) * 12)
#define TDMA_RING(r)        (0x4c00 + (r) * 0x40)
#define TRING_READ_PTR      0x00
#define TRING_CONS_INDEX    0x08
#define TRING_PROD_INDEX    0x0c
#define TDMA_RING_CFG       0x5040
#define TDMA_CTRL           0x5044
#define TSB_LEN             64      /* transmit status block */

#define DESCS_PER_RING      4
#define BUF_SIZE            2048
#define BUF_BASE            0x100000
#define FRAME_LEN           60

static void genet_writel(QTestState *qts, uint32_t off, uint32_t val)
{
    qtest_writel(qts, GENET_BASE + off, val);
}

static uint32_t genet_readl(QTestState *qts, uint32_t off)
{
    return qtest_readl(qts, GENET_BASE + off);
}

/* Transmit across the wrap of the 16-bit consumer index */
static void test_tx_index_wrap(void)
{
    const unsigned r = 0, n = 3;
    const uint16_t first = 0xfffe;
    uint8_t frame[FRAME_LEN] = { 0 };
    uint8_t pkt[4 + FRAME_LEN];
    int sv[2];
    QTestState *qts;

    g_assert_cmpint(socketpair(PF_UNIX, SOCK_STREAM, 0, sv), ==, 0);
    qts = qtest_initf("-M raspi4b -nic socket,fd=%d,model=bcm2838-genet",
                      sv[1]);
    close(sv[1]);

    memset(frame, 0xff, 6);
    stw_be_p(frame + 12, 0x88b5);
    for (unsigned i = 0; i < n; i++) {
        frame[14] = i;
        qtest_memset(qts, BUF_BASE + i * BUF_SIZE, 0, TSB_LEN);
        qtest_memwrite(qts, BUF_BASE + i * BUF_SIZE + TSB_LEN, frame,
                       sizeof(frame));
        /* SOP, EOP, buffer length */
        genet_writel(qts, TDMA_DESC(i), BIT(13) | BIT(14) |
                     (TSB_LEN + FRAME_LEN) << 16);
        genet_writel(qts, TDMA_DESC(i) + 4, BUF_BASE + i * BUF_SIZE);
        genet_writel(qts, TDMA_DESC(i) + 8, 0);
    }
    genet_writel(qts, TDMA_RING(r) + RING_START_ADDR, 0);
    genet_writel(qts, TDMA_RING(r) + RING_END_ADDR, DESCS_PER_RING * 3 - 1);
    genet_writel(qts, TDMA_RING(r) + TRING_READ_PTR, 0);
    genet_writel(qts, TDMA_RING(r) + TRING_CONS_INDEX, first);
    genet_writel(qts, TDMA_RING(r) + TRING_PROD_INDEX, first);
    genet_writel(qts, TDMA_RING_CFG, BIT(r));
    genet_writel(qts, TDMA_CTRL, 1 | BIT(1 + r));

    genet_writel(qts, TDMA_RING(r) + TRING_PROD_INDEX,
                 (uint16_t)(first + n));
    g_assert_cmphex(genet_readl(qts, TDMA_RING(r) + TRING_CONS_INDEX) &
                    0xffff, ==, (uint16_t)(first + n));

    for (unsigned i = 0; i < n; i++) {
        g_assert_cmpint(recv(sv[0], pkt, sizeof(pkt), MSG_WAITALL), ==,
                        sizeof(pkt));
        g_assert_cmpuint(ldl_be_p(pkt), ==, FRAME_LEN);
        g_assert_cmpuint(pkt[4 + 14], ==, i);
    }

    qtest_quit(qts);
    close(sv[0]);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/bcm2838-genet/tx-index-wrap", test_tx_index_wrap);
    return g_test_run();
}
