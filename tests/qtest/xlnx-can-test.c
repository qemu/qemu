/*
 * QTests for the Xilinx ZynqMP CAN controller.
 *
 * Copyright (c) 2020 Xilinx Inc.
 *
 * Written-by: Vikram Garhwal<fnu.vikram@xilinx.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "libqtest.h"

/* Base address. */
#define CAN0_BASE_ADDR          0xFF060000
#define CAN1_BASE_ADDR          0xFF070000

/* Register addresses. */
#define R_SRR_OFFSET            0x00
#define R_MSR_OFFSET            0x04
#define R_SR_OFFSET             0x18
#define R_ISR_OFFSET            0x1C
#define R_ICR_OFFSET            0x24
#define R_TXID_OFFSET           0x30
#define R_TXDLC_OFFSET          0x34
#define R_TXDATA1_OFFSET        0x38
#define R_TXDATA2_OFFSET        0x3C
#define R_RXID_OFFSET           0x50
#define R_RXDLC_OFFSET          0x54
#define R_RXDATA1_OFFSET        0x58
#define R_RXDATA2_OFFSET        0x5C
#define R_AFR                   0x60
#define R_AFMR1                 0x64
#define R_AFIR1                 0x68
#define R_AFMR2                 0x6C
#define R_AFIR2                 0x70
#define R_AFMR3                 0x74
#define R_AFIR3                 0x78
#define R_AFMR4                 0x7C
#define R_AFIR4                 0x80

/* CAN modes. */
#define CONFIG_MODE             0x00
#define NORMAL_MODE             0x00
#define LOOPBACK_MODE           0x02
#define SNOOP_MODE              0x04
#define SLEEP_MODE              0x01
#define ENABLE_CAN              (1 << 1)
#define STATUS_NORMAL_MODE      (1 << 3)
#define STATUS_LOOPBACK_MODE    (1 << 1)
#define STATUS_SNOOP_MODE       (1 << 12)
#define STATUS_SLEEP_MODE       (1 << 2)
#define ISR_TXOK                (1 << 1)
#define ISR_RXOK                (1 << 4)

static void match_rx_tx_data(const uint32_t *buf_tx, const uint32_t *buf_rx,
                             uint8_t can_timestamp)
{
    uint16_t size = 0;
    uint8_t len = 4;

    while (size < len) {
        if (R_RXID_OFFSET + 4 * size == R_RXDLC_OFFSET)  {
            g_assert_cmpint(buf_rx[size], ==, buf_tx[size] + can_timestamp);
        } else {
            g_assert_cmpint(buf_rx[size], ==, buf_tx[size]);
        }

        size++;
    }
}

static void read_data(QTestState *qts, uint64_t can_base_addr, uint32_t *buf_rx)
{
    uint32_t int_status;

    /* Read the interrupt on CAN rx. */
    int_status = qtest_readl(qts, can_base_addr + R_ISR_OFFSET) & ISR_RXOK;

    g_assert_cmpint(int_status, ==, ISR_RXOK);

    /* Read the RX register data for CAN. */
    buf_rx[0] = qtest_readl(qts, can_base_addr + R_RXID_OFFSET);
    buf_rx[1] = qtest_readl(qts, can_base_addr + R_RXDLC_OFFSET);
    buf_rx[2] = qtest_readl(qts, can_base_addr + R_RXDATA1_OFFSET);
    buf_rx[3] = qtest_readl(qts, can_base_addr + R_RXDATA2_OFFSET);

    /* Clear the RX interrupt. */
    qtest_writel(qts, CAN1_BASE_ADDR + R_ICR_OFFSET, ISR_RXOK);
}

static void send_data(QTestState *qts, uint64_t can_base_addr,
                      const uint32_t *buf_tx)
{
    uint32_t int_status;

    /* Write the TX register data for CAN. */
    qtest_writel(qts, can_base_addr + R_TXID_OFFSET, buf_tx[0]);
    qtest_writel(qts, can_base_addr + R_TXDLC_OFFSET, buf_tx[1]);
    qtest_writel(qts, can_base_addr + R_TXDATA1_OFFSET, buf_tx[2]);
    qtest_writel(qts, can_base_addr + R_TXDATA2_OFFSET, buf_tx[3]);

    /* Read the interrupt on CAN for tx. */
    int_status = qtest_readl(qts, can_base_addr + R_ISR_OFFSET) & ISR_TXOK;

    g_assert_cmpint(int_status, ==, ISR_TXOK);

    /* Clear the interrupt for tx. */
    qtest_writel(qts, CAN0_BASE_ADDR + R_ICR_OFFSET, ISR_TXOK);
}

/*
 * This test will be transferring data from CAN0 and CAN1 through canbus. CAN0
 * initiate the data transfer to can-bus, CAN1 receives the data. Test compares
 * the data sent from CAN0 with received on CAN1.
 */
static void test_can_bus(void)
{
    const uint32_t buf_tx[4] = { 0xFF, 0x80000000, 0x12345678, 0x87654321 };
    uint32_t buf_rx[4] = { 0x00, 0x00, 0x00, 0x00 };
    uint32_t status = 0;
    uint8_t can_timestamp = 1;

    QTestState *qts = qtest_init("-machine xlnx-zcu102"
                " -object can-bus,id=canbus"
                " -machine canbus0=canbus"
                " -machine canbus1=canbus"
                );

    /* Configure the CAN0 and CAN1. */
    qtest_writel(qts, CAN0_BASE_ADDR + R_SRR_OFFSET, ENABLE_CAN);
    qtest_writel(qts, CAN0_BASE_ADDR + R_MSR_OFFSET, NORMAL_MODE);
    qtest_writel(qts, CAN1_BASE_ADDR + R_SRR_OFFSET, ENABLE_CAN);
    qtest_writel(qts, CAN1_BASE_ADDR + R_MSR_OFFSET, NORMAL_MODE);

    /* Check here if CAN0 and CAN1 are in normal mode. */
    status = qtest_readl(qts, CAN0_BASE_ADDR + R_SR_OFFSET);
    g_assert_cmpint(status, ==, STATUS_NORMAL_MODE);

    status = qtest_readl(qts, CAN1_BASE_ADDR + R_SR_OFFSET);
    g_assert_cmpint(status, ==, STATUS_NORMAL_MODE);

    send_data(qts, CAN0_BASE_ADDR, buf_tx);

    read_data(qts, CAN1_BASE_ADDR, buf_rx);
    match_rx_tx_data(buf_tx, buf_rx, can_timestamp);

    qtest_quit(qts);
}

/*
 * This test is performing loopback mode on CAN0 and CAN1. Data sent from TX of
 * each CAN0 and CAN1 are compared with RX register data for respective CAN.
 */
static void test_can_loopback(void)
{
    uint32_t buf_tx[4] = { 0xFF, 0x80000000, 0x12345678, 0x87654321 };
    uint32_t buf_rx[4] = { 0x00, 0x00, 0x00, 0x00 };
    uint32_t status = 0;

    QTestState *qts = qtest_init("-machine xlnx-zcu102"
                " -object can-bus,id=canbus"
                " -machine canbus0=canbus"
                " -machine canbus1=canbus"
                );

    /* Configure the CAN0 in loopback mode. */
    qtest_writel(qts, CAN0_BASE_ADDR + R_SRR_OFFSET, CONFIG_MODE);
    qtest_writel(qts, CAN0_BASE_ADDR + R_MSR_OFFSET, LOOPBACK_MODE);
    qtest_writel(qts, CAN0_BASE_ADDR + R_SRR_OFFSET, ENABLE_CAN);

    /* Check here if CAN0 is set in loopback mode. */
    status = qtest_readl(qts, CAN0_BASE_ADDR + R_SR_OFFSET);

    g_assert_cmpint(status, ==, STATUS_LOOPBACK_MODE);

    send_data(qts, CAN0_BASE_ADDR, buf_tx);
    read_data(qts, CAN0_BASE_ADDR, buf_rx);
    match_rx_tx_data(buf_tx, buf_rx, 0);

    /* Configure the CAN1 in loopback mode. */
    qtest_writel(qts, CAN1_BASE_ADDR + R_SRR_OFFSET, CONFIG_MODE);
    qtest_writel(qts, CAN1_BASE_ADDR + R_MSR_OFFSET, LOOPBACK_MODE);
    qtest_writel(qts, CAN1_BASE_ADDR + R_SRR_OFFSET, ENABLE_CAN);

    /* Check here if CAN1 is set in loopback mode. */
    status = qtest_readl(qts, CAN1_BASE_ADDR + R_SR_OFFSET);

    g_assert_cmpint(status, ==, STATUS_LOOPBACK_MODE);

    send_data(qts, CAN1_BASE_ADDR, buf_tx);
    read_data(qts, CAN1_BASE_ADDR, buf_rx);
    match_rx_tx_data(buf_tx, buf_rx, 0);

    qtest_quit(qts);
}

/*
 * Enable filters for CAN1. This will filter incoming messages with ID. In this
 * test message will pass through filter 2.
 */
static void test_can_filter(void)
{
    uint32_t buf_tx[4] = { 0x14, 0x80000000, 0x12345678, 0x87654321 };
    uint32_t buf_rx[4] = { 0x00, 0x00, 0x00, 0x00 };
    uint32_t status = 0;
    uint8_t can_timestamp = 1;

    QTestState *qts = qtest_init("-machine xlnx-zcu102"
                " -object can-bus,id=canbus"
                " -machine canbus0=canbus"
                " -machine canbus1=canbus"
                );

    /* Configure the CAN0 and CAN1. */
    qtest_writel(qts, CAN0_BASE_ADDR + R_SRR_OFFSET, ENABLE_CAN);
    qtest_writel(qts, CAN0_BASE_ADDR + R_MSR_OFFSET, NORMAL_MODE);
    qtest_writel(qts, CAN1_BASE_ADDR + R_SRR_OFFSET, ENABLE_CAN);
    qtest_writel(qts, CAN1_BASE_ADDR + R_MSR_OFFSET, NORMAL_MODE);

    /* Check here if CAN0 and CAN1 are in normal mode. */
    status = qtest_readl(qts, CAN0_BASE_ADDR + R_SR_OFFSET);
    g_assert_cmpint(status, ==, STATUS_NORMAL_MODE);

    status = qtest_readl(qts, CAN1_BASE_ADDR + R_SR_OFFSET);
    g_assert_cmpint(status, ==, STATUS_NORMAL_MODE);

    /* Set filter for CAN1 for incoming messages. */
    qtest_writel(qts, CAN1_BASE_ADDR + R_AFR, 0x0);
    qtest_writel(qts, CAN1_BASE_ADDR + R_AFMR1, 0xF7);
    qtest_writel(qts, CAN1_BASE_ADDR + R_AFIR1, 0x121F);
    qtest_writel(qts, CAN1_BASE_ADDR + R_AFMR2, 0x5431);
    qtest_writel(qts, CAN1_BASE_ADDR + R_AFIR2, 0x14);
    qtest_writel(qts, CAN1_BASE_ADDR + R_AFMR3, 0x1234);
    qtest_writel(qts, CAN1_BASE_ADDR + R_AFIR3, 0x5431);
    qtest_writel(qts, CAN1_BASE_ADDR + R_AFMR4, 0xFFF);
    qtest_writel(qts, CAN1_BASE_ADDR + R_AFIR4, 0x1234);

    qtest_writel(qts, CAN1_BASE_ADDR + R_AFR, 0xF);

    send_data(qts, CAN0_BASE_ADDR, buf_tx);

    read_data(qts, CAN1_BASE_ADDR, buf_rx);
    match_rx_tx_data(buf_tx, buf_rx, can_timestamp);

    qtest_quit(qts);
}

/* Testing sleep mode on CAN0 while CAN1 is in normal mode. */
static void test_can_sleepmode(void)
{
    uint32_t buf_tx[4] = { 0x14, 0x80000000, 0x12345678, 0x87654321 };
    uint32_t buf_rx[4] = { 0x00, 0x00, 0x00, 0x00 };
    uint32_t status = 0;
    uint8_t can_timestamp = 1;

    QTestState *qts = qtest_init("-machine xlnx-zcu102"
                " -object can-bus,id=canbus"
                " -machine canbus0=canbus"
                " -machine canbus1=canbus"
                );

    /* Configure the CAN0. */
    qtest_writel(qts, CAN0_BASE_ADDR + R_SRR_OFFSET, CONFIG_MODE);
    qtest_writel(qts, CAN0_BASE_ADDR + R_MSR_OFFSET, SLEEP_MODE);
    qtest_writel(qts, CAN0_BASE_ADDR + R_SRR_OFFSET, ENABLE_CAN);

    qtest_writel(qts, CAN1_BASE_ADDR + R_SRR_OFFSET, ENABLE_CAN);
    qtest_writel(qts, CAN1_BASE_ADDR + R_MSR_OFFSET, NORMAL_MODE);

    /* Check here if CAN0 is in SLEEP mode and CAN1 in normal mode. */
    status = qtest_readl(qts, CAN0_BASE_ADDR + R_SR_OFFSET);
    g_assert_cmpint(status, ==, STATUS_SLEEP_MODE);

    status = qtest_readl(qts, CAN1_BASE_ADDR + R_SR_OFFSET);
    g_assert_cmpint(status, ==, STATUS_NORMAL_MODE);

    send_data(qts, CAN1_BASE_ADDR, buf_tx);

    /*
     * Once CAN1 sends data on can-bus. CAN0 should exit sleep mode.
     * Check the CAN0 status now. It should exit the sleep mode and receive the
     * incoming data.
     */
    status = qtest_readl(qts, CAN0_BASE_ADDR + R_SR_OFFSET);
    g_assert_cmpint(status, ==, STATUS_NORMAL_MODE);

    read_data(qts, CAN0_BASE_ADDR, buf_rx);

    match_rx_tx_data(buf_tx, buf_rx, can_timestamp);

    qtest_quit(qts);
}

/* Testing Snoop mode on CAN0 while CAN1 is in normal mode. */
static void test_can_snoopmode(void)
{
    uint32_t buf_tx[4] = { 0x14, 0x80000000, 0x12345678, 0x87654321 };
    uint32_t buf_rx[4] = { 0x00, 0x00, 0x00, 0x00 };
    uint32_t status = 0;
    uint8_t can_timestamp = 1;

    QTestState *qts = qtest_init("-machine xlnx-zcu102"
                " -object can-bus,id=canbus"
                " -machine canbus0=canbus"
                " -machine canbus1=canbus"
                );

    /* Configure the CAN0. */
    qtest_writel(qts, CAN0_BASE_ADDR + R_SRR_OFFSET, CONFIG_MODE);
    qtest_writel(qts, CAN0_BASE_ADDR + R_MSR_OFFSET, SNOOP_MODE);
    qtest_writel(qts, CAN0_BASE_ADDR + R_SRR_OFFSET, ENABLE_CAN);

    qtest_writel(qts, CAN1_BASE_ADDR + R_SRR_OFFSET, ENABLE_CAN);
    qtest_writel(qts, CAN1_BASE_ADDR + R_MSR_OFFSET, NORMAL_MODE);

    /* Check here if CAN0 is in SNOOP mode and CAN1 in normal mode. */
    status = qtest_readl(qts, CAN0_BASE_ADDR + R_SR_OFFSET);
    g_assert_cmpint(status, ==, STATUS_SNOOP_MODE);

    status = qtest_readl(qts, CAN1_BASE_ADDR + R_SR_OFFSET);
    g_assert_cmpint(status, ==, STATUS_NORMAL_MODE);

    send_data(qts, CAN1_BASE_ADDR, buf_tx);

    read_data(qts, CAN0_BASE_ADDR, buf_rx);

    match_rx_tx_data(buf_tx, buf_rx, can_timestamp);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/net/can/can_bus", test_can_bus);
    qtest_add_func("/net/can/can_loopback", test_can_loopback);
    qtest_add_func("/net/can/can_filter", test_can_filter);
    qtest_add_func("/net/can/can_test_snoopmode", test_can_snoopmode);
    qtest_add_func("/net/can/can_test_sleepmode", test_can_sleepmode);

    return g_test_run();
}
