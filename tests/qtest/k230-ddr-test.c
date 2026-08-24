/*
 * QTest testcase for the K230 DDR controller and PHY
 *
 * Exercises register access, reset, PHY ownership, training mailbox, and DFI
 * initialization.
 *
 * Copyright (c) 2026 Junze Cao <caojunze424@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define K230_DDRC_BASE    UINT64_C(0x98000000)
#define K230_DDR_PHY_BASE UINT64_C(0x9a000000)

#define K230_DDRC_MSTR    0x000
#define K230_DDRC_STAT    0x004
#define K230_DDRC_DRAMTMG4 0x110
#define K230_DDRC_DFIMISC 0x1b0
#define K230_DDRC_DFISTAT 0x1bc
#define K230_DDRC_SWCTL   0x320
#define K230_DDRC_SWSTAT  0x324

#define K230_DDRC_STAT_OPERATING_MODE_MASK          0x7
#define K230_DDRC_DFISTAT_DFI_INIT_COMPLETE_MASK    0x1

#define K230_DDR_PHY_CSR(index) \
    (K230_DDR_PHY_BASE + (uint64_t)(index) * sizeof(uint32_t))

#define K230_DDR_PHY_ATX_IMPEDANCE      0x00043
#define K230_DDR_PHY_TX_IMPEDANCE_CTRL1 0x10049
#define K230_DDR_PHY_VREF_IN_GLOBAL     0x200b2
#define K230_DDR_PHY_MICRO_CONT_MUX_SEL 0xd0000
#define K230_DDR_PHY_TRAINING_STATUS    0xd0004
#define K230_DDR_PHY_TRAINING_ACK       0xd0031
#define K230_DDR_PHY_TRAINING_MESSAGE   0xd0032
#define K230_DDR_PHY_TRAINING_TRIGGER   0xd0099

static void complete_phy_training(QTestState *qts)
{
    qtest_writel(qts,
                 K230_DDR_PHY_CSR(K230_DDR_PHY_MICRO_CONT_MUX_SEL), 1);
    qtest_writel(qts, K230_DDR_PHY_CSR(K230_DDR_PHY_TRAINING_TRIGGER), 9);
    qtest_writel(qts, K230_DDR_PHY_CSR(K230_DDR_PHY_TRAINING_TRIGGER), 1);
    qtest_writel(qts, K230_DDR_PHY_CSR(K230_DDR_PHY_TRAINING_TRIGGER), 0);
}

static void test_reset_and_register_access(void)
{
    QTestState *qts = qtest_init("-machine k230");

    g_assert_cmphex(qtest_readl(qts, K230_DDRC_BASE + K230_DDRC_MSTR),
                    ==, 0x01040000);
    g_assert_cmphex(qtest_readl(qts, K230_DDRC_BASE + K230_DDRC_STAT),
                    ==, 0x00000000);
    g_assert_cmphex(qtest_readl(qts, K230_DDRC_BASE + K230_DDRC_DRAMTMG4),
                    ==, 0x05040405);
    g_assert_cmphex(qtest_readl(qts, K230_DDRC_BASE + K230_DDRC_DFIMISC),
                    ==, 0x00000001);
    g_assert_cmphex(qtest_readl(qts, K230_DDRC_BASE + K230_DDRC_SWCTL),
                    ==, 0x00000001);
    g_assert_cmphex(qtest_readl(qts, K230_DDRC_BASE + K230_DDRC_SWSTAT),
                    ==, 0x00000001);

    g_assert_cmphex(qtest_readl(qts,
                    K230_DDR_PHY_CSR(K230_DDR_PHY_ATX_IMPEDANCE)),
                    ==, 0x03ff);
    g_assert_cmphex(qtest_readl(qts,
                    K230_DDR_PHY_CSR(K230_DDR_PHY_TX_IMPEDANCE_CTRL1)),
                    ==, 0x0fff);
    g_assert_cmphex(qtest_readl(qts,
                    K230_DDR_PHY_CSR(K230_DDR_PHY_VREF_IN_GLOBAL)),
                    ==, 0x0200);

    qtest_writel(qts, K230_DDRC_BASE + K230_DDRC_MSTR, 0x01040008);
    g_assert_cmphex(qtest_readl(qts, K230_DDRC_BASE + K230_DDRC_MSTR),
                    ==, 0x01040008);

    qtest_writel(qts, K230_DDRC_BASE + K230_DDRC_STAT, 0xffffffff);
    g_assert_cmphex(qtest_readl(qts, K230_DDRC_BASE + K230_DDRC_STAT),
                    ==, 0x00000000);

    qtest_writel(qts, K230_DDR_PHY_CSR(K230_DDR_PHY_VREF_IN_GLOBAL),
                 0xffffffff);
    g_assert_cmphex(qtest_readl(qts,
                    K230_DDR_PHY_CSR(K230_DDR_PHY_VREF_IN_GLOBAL)),
                    ==, 0x7fff);

    qtest_writel(qts, K230_DDRC_BASE + K230_DDRC_DRAMTMG4, 0);

    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, K230_DDRC_BASE + K230_DDRC_MSTR),
                    ==, 0x01040000);
    g_assert_cmphex(qtest_readl(qts, K230_DDRC_BASE + K230_DDRC_DRAMTMG4),
                    ==, 0x05040405);
    g_assert_cmphex(qtest_readl(qts,
                    K230_DDR_PHY_CSR(K230_DDR_PHY_VREF_IN_GLOBAL)),
                    ==, 0x0200);

    qtest_quit(qts);
}

static void test_software_update_handshake(void)
{
    QTestState *qts = qtest_init("-machine k230");

    qtest_writel(qts, K230_DDRC_BASE + K230_DDRC_SWCTL, 0);
    g_assert_cmphex(qtest_readl(qts, K230_DDRC_BASE + K230_DDRC_SWSTAT),
                    ==, 0);

    qtest_writel(qts, K230_DDRC_BASE + K230_DDRC_SWCTL, 1);
    g_assert_cmphex(qtest_readl(qts, K230_DDRC_BASE + K230_DDRC_SWSTAT),
                    ==, 1);

    qtest_writel(qts, K230_DDRC_BASE + K230_DDRC_SWSTAT, 0);
    g_assert_cmphex(qtest_readl(qts, K230_DDRC_BASE + K230_DDRC_SWSTAT),
                    ==, 1);

    qtest_quit(qts);
}

static void test_phy_ownership(void)
{
    QTestState *qts = qtest_init("-machine k230");
    uint64_t atx = K230_DDR_PHY_CSR(K230_DDR_PHY_ATX_IMPEDANCE);
    uint64_t mux = K230_DDR_PHY_CSR(K230_DDR_PHY_MICRO_CONT_MUX_SEL);

    qtest_writel(qts, atx, 0x155);
    g_assert_cmphex(qtest_readl(qts, atx), ==, 0x155);

    qtest_writel(qts, mux, 1);
    qtest_writel(qts, atx, 0x2aa);
    g_assert_cmphex(qtest_readl(qts, atx), ==, 0x155);

    qtest_writel(qts, mux, 0);
    qtest_writel(qts, atx, 0x2aa);
    g_assert_cmphex(qtest_readl(qts, atx), ==, 0x2aa);

    qtest_quit(qts);
}

static void test_dfi_prerequisites(void)
{
    QTestState *qts = qtest_init("-machine k230");

    qtest_writel(qts, K230_DDRC_BASE + K230_DDRC_DFIMISC, 0x20);

    g_assert_cmphex(qtest_readl(qts,
                    K230_DDRC_BASE + K230_DDRC_DFISTAT) &
                    K230_DDRC_DFISTAT_DFI_INIT_COMPLETE_MASK,
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, K230_DDRC_BASE + K230_DDRC_STAT) &
                    K230_DDRC_STAT_OPERATING_MODE_MASK,
                    ==, 0);

    qtest_quit(qts);
}

static void test_dfi_complete_enable(void)
{
    QTestState *qts = qtest_init("-machine k230");

    complete_phy_training(qts);
    qtest_writel(qts, K230_DDRC_BASE + K230_DDRC_DFIMISC, 0x20);

    g_assert_cmphex(qtest_readl(qts,
                    K230_DDRC_BASE + K230_DDRC_DFISTAT) &
                    K230_DDRC_DFISTAT_DFI_INIT_COMPLETE_MASK,
                    ==, 1);
    g_assert_cmphex(qtest_readl(qts, K230_DDRC_BASE + K230_DDRC_STAT) &
                    K230_DDRC_STAT_OPERATING_MODE_MASK,
                    ==, 0);

    qtest_quit(qts);
}

static void test_training_and_dfi_handshake(void)
{
    QTestState *qts = qtest_init("-machine k230");

    complete_phy_training(qts);

    g_assert_cmphex(qtest_readl(qts,
                    K230_DDR_PHY_CSR(K230_DDR_PHY_TRAINING_STATUS)),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                    K230_DDR_PHY_CSR(K230_DDR_PHY_TRAINING_MESSAGE)),
                    ==, 0x07);

    qtest_writel(qts, K230_DDR_PHY_CSR(K230_DDR_PHY_TRAINING_ACK), 0);
    g_assert_cmphex(qtest_readl(qts,
                    K230_DDR_PHY_CSR(K230_DDR_PHY_TRAINING_STATUS)),
                    ==, 1);
    qtest_writel(qts, K230_DDR_PHY_CSR(K230_DDR_PHY_TRAINING_ACK), 1);

    qtest_writel(qts, K230_DDRC_BASE + K230_DDRC_DFIMISC, 0x20);
    g_assert_cmphex(qtest_readl(qts, K230_DDRC_BASE + K230_DDRC_DFISTAT),
                    ==, 1);

    qtest_writel(qts, K230_DDRC_BASE + K230_DDRC_DFIMISC, 0);
    qtest_writel(qts, K230_DDRC_BASE + K230_DDRC_DFIMISC, 1);
    g_assert_cmphex(qtest_readl(qts, K230_DDRC_BASE + K230_DDRC_STAT) & 0x7,
                    ==, 1);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/k230-ddr/reset-and-register-access",
                   test_reset_and_register_access);
    qtest_add_func("/k230-ddr/software-update-handshake",
                   test_software_update_handshake);
    qtest_add_func("/k230-ddr/phy-ownership", test_phy_ownership);
    qtest_add_func("/k230-ddr/dfi-prerequisites", test_dfi_prerequisites);
    qtest_add_func("/k230-ddr/dfi-complete-enable",
                   test_dfi_complete_enable);
    qtest_add_func("/k230-ddr/training-and-dfi-handshake",
                   test_training_and_dfi_handshake);

    return g_test_run();
}
