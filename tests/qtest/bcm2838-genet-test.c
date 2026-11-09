/*
 * QTest testcase for the BCM2838 GENET DMA rings
 *
 * Frames come in and go out over a socket netdev. The producer and
 * consumer indexes of a ring count modulo 2^16.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"
#include "libqtest.h"

#define GENET_BASE          0xfd580000

#define RDMA_DESC(i)        (0x2000 + (i) * 12)
#define RDMA_RING(r)        (0x2c00 + (r) * 0x40)
#define RING_WRITE_PTR      0x00
#define RING_PROD_INDEX     0x08
#define RING_CONS_INDEX     0x0c
#define RING_BUF_SIZE       0x10
#define RING_START_ADDR     0x14
#define RING_END_ADDR       0x1c
#define RDMA_RING_CFG       0x3040
#define RDMA_CTRL           0x3044

#define TDMA_DESC(i)        (0x4000 + (i) * 12)
#define TDMA_RING(r)        (0x4c00 + (r) * 0x40)
#define TRING_READ_PTR      0x00
#define TRING_CONS_INDEX    0x08
#define TRING_PROD_INDEX    0x0c
#define TDMA_RING_CFG       0x5040
#define TDMA_CTRL           0x5044
#define TSB_LEN             64      /* transmit status block */

#define DEFAULT_RING        16
#define DESCS_PER_RING      4
#define BUF_SIZE            2048
#define BUF_BASE            0x100000
#define STATUS_LEN          64      /* receive status block, then 2 bytes */
#define FRAME_LEN           60

static void genet_writel(QTestState *qts, uint32_t off, uint32_t val)
{
    qtest_writel(qts, GENET_BASE + off, val);
}

static uint32_t genet_readl(QTestState *qts, uint32_t off)
{
    return qtest_readl(qts, GENET_BASE + off);
}

/* The default ring uses the first DESCS_PER_RING descriptors */
static QTestState *genet_rx_init(int *fd)
{
    const unsigned r = DEFAULT_RING;
    int sv[2];
    QTestState *qts;

    g_assert_cmpint(socketpair(PF_UNIX, SOCK_STREAM, 0, sv), ==, 0);
    qts = qtest_initf("-M raspi4b -nic socket,fd=%d,model=bcm2838-genet",
                      sv[1]);
    close(sv[1]);
    *fd = sv[0];

    for (unsigned i = 0; i < DESCS_PER_RING; i++) {
        genet_writel(qts, RDMA_DESC(i) + 4, BUF_BASE + i * BUF_SIZE);
        genet_writel(qts, RDMA_DESC(i) + 8, 0);
    }
    genet_writel(qts, RDMA_RING(r) + RING_START_ADDR, 0);
    genet_writel(qts, RDMA_RING(r) + RING_END_ADDR, (DESCS_PER_RING - 1) * 3);
    genet_writel(qts, RDMA_RING(r) + RING_WRITE_PTR, 0);
    genet_writel(qts, RDMA_RING(r) + RING_BUF_SIZE, BUF_SIZE);
    genet_writel(qts, RDMA_RING_CFG, BIT(r));
    genet_writel(qts, RDMA_CTRL, 1 | BIT(1 + r));
    return qts;
}

static uint32_t prod_index(QTestState *qts, unsigned r)
{
    return genet_readl(qts, RDMA_RING(r) + RING_PROD_INDEX) & 0xffff;
}

/* Send a broadcast frame into the socket netdev */
static void send_frame(int fd, uint16_t ethertype, uint8_t byte14)
{
    uint8_t pkt[4 + FRAME_LEN] = { 0 };

    stl_be_p(pkt, FRAME_LEN);
    memset(pkt + 4, 0xff, 6);
    stw_be_p(pkt + 4 + 12, ethertype);
    pkt[4 + 14] = byte14;
    g_assert_cmpint(send(fd, pkt, sizeof(pkt), 0), ==, sizeof(pkt));
}

/* The byte at 14 of the frame that descriptor I holds */
static uint8_t rx_byte14(QTestState *qts, unsigned i)
{
    return qtest_readb(qts, BUF_BASE + i * BUF_SIZE + STATUS_LEN + 2 + 14);
}

/* Wait up to MS milliseconds for the producer index of ring R to be N */
static bool wait_prod_index(QTestState *qts, unsigned r, uint32_t n,
                            unsigned ms)
{
    gint64 end = g_get_monotonic_time() + ms * G_TIME_SPAN_MILLISECOND;

    do {
        qtest_clock_step(qts, 100);
        if (prod_index(qts, r) == n) {
            return true;
        }
    } while (g_get_monotonic_time() < end);
    return false;
}

/* A full RX ring keeps the next frames until the guest frees descriptors */
static void test_rx_ring_full(void)
{
    const unsigned n = DESCS_PER_RING + 2;
    int fd;
    QTestState *qts = genet_rx_init(&fd);

    for (unsigned i = 0; i < n; i++) {
        send_frame(fd, 0x0800, i);
    }
    g_assert(wait_prod_index(qts, DEFAULT_RING, DESCS_PER_RING, 5000));
    /* the rest must not overwrite descriptors the guest has not read */
    g_assert(!wait_prod_index(qts, DEFAULT_RING, DESCS_PER_RING + 1, 500));
    for (unsigned i = 0; i < DESCS_PER_RING; i++) {
        g_assert_cmpuint(rx_byte14(qts, i), ==, i);
    }

    genet_writel(qts, RDMA_RING(DEFAULT_RING) + RING_CONS_INDEX, 2);
    g_assert(wait_prod_index(qts, DEFAULT_RING, n, 5000));
    for (unsigned i = 0; i < n; i++) {
        g_assert_cmpuint(rx_byte14(qts, i % DESCS_PER_RING), ==,
                         i < 2 ? i + DESCS_PER_RING : i);
    }

    qtest_quit(qts);
    close(fd);
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
    qtest_add_func("/bcm2838-genet/rx-ring-full", test_rx_ring_full);
    qtest_add_func("/bcm2838-genet/tx-index-wrap", test_tx_index_wrap);
    return g_test_run();
}
