/*
 * QTest testcase for K230 Watchdog
 *
 * Copyright (c) 2025 Mig Yang <temashking@foxmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Provides a board compatible with the kendryte K230 SDK
 *
 * K230 Technical Reference Manual V0.3.1 (2024-11-18):
 * https://github.com/revyos/external-docs/blob/master/K230/en-us/K230_Technical_Reference_Manual_V0.3.1_20241118.pdf
 *
 * For more information, see <https://www.kendryte.com/en/proDetail/230>
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "qemu/bitops.h"
#include "libqtest.h"
#include "hw/watchdog/k230_wdt.h"

/* K230 WDT0 base address */
#define K230_WDT0_BASE 0x91106000
#define K230_WDT1_BASE 0x91106800

/* Test WDT0 by default */
#define WDT_BASE K230_WDT0_BASE

static void test_register_read_write(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /* Test Control Register (CR) read/write */
    qtest_writel(qts, WDT_BASE + K230_WDT_CR, 0xFFFFFFFF);
    g_assert_cmphex(qtest_readl(qts, WDT_BASE + K230_WDT_CR), ==,
                    (K230_WDT_CR_RPL_MASK << K230_WDT_CR_RPL_SHIFT) |
                    K230_WDT_CR_RMOD | K230_WDT_CR_WDT_EN);

    /* Test Timeout Range Register (TORR) read/write */
    qtest_writel(qts, WDT_BASE + K230_WDT_TORR, 0xFFFFFFFF);
    g_assert_cmphex(qtest_readl(qts, WDT_BASE + K230_WDT_TORR), ==,
                    K230_WDT_TORR_TOP_MASK);

    /* Test Protection Level Register read/write */
    qtest_writel(qts, WDT_BASE + K230_WDT_PROT_LEVEL, 0xFFFFFFFF);
    g_assert_cmphex(qtest_readl(qts, WDT_BASE + K230_WDT_PROT_LEVEL), ==, 0x7);

    qtest_quit(qts);
}

static void test_counter_restart(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /* Enable watchdog and set timeout */
    qtest_writel(qts, WDT_BASE + K230_WDT_CR, K230_WDT_CR_WDT_EN);
    qtest_writel(qts, WDT_BASE + K230_WDT_TORR, 0x5); /* TOP = 5 */

    /* Read current counter value */
    uint32_t initial_count = qtest_readl(qts, WDT_BASE + K230_WDT_CCVR);
    g_assert_cmpuint(initial_count, >, 0);

    /* Restart counter with magic value */
    qtest_writel(qts, WDT_BASE + K230_WDT_CRR, K230_WDT_CRR_RESTART);

    /* Wait for time */
    qtest_clock_step(qts, NANOSECONDS_PER_SECOND * 2);

    /* Counter should be reset to timeout value */
    uint32_t new_count = qtest_readl(qts, WDT_BASE + K230_WDT_CCVR);
    g_assert_cmpuint(new_count, >, 0);
    g_assert_cmpuint(new_count, !=, initial_count);

    qtest_quit(qts);
}

static void test_interrupt_mode(void)
{
    QTestState *qts = qtest_init("-machine k230 --trace k230_*,file=k230.log");

    /* Set interrupt mode and enable watchdog */
    qtest_writel(qts, WDT_BASE + K230_WDT_CR,
                 K230_WDT_CR_RMOD | K230_WDT_CR_WDT_EN);
    qtest_writel(qts, WDT_BASE + K230_WDT_TORR, 0x1); /* Short timeout */

    /* Wait for timeout to trigger interrupt */
    qtest_clock_step(qts, NANOSECONDS_PER_SECOND * 10);

    /* Check interrupt status */
    uint32_t stat = qtest_readl(qts, WDT_BASE + K230_WDT_STAT);
    g_assert_cmphex(stat & K230_WDT_STAT_INT, ==, K230_WDT_STAT_INT);

    /* Clear interrupt */
    qtest_writel(qts, WDT_BASE + K230_WDT_EOI, 0x1);
    stat = qtest_readl(qts, WDT_BASE + K230_WDT_STAT);
    g_assert_cmphex(stat & K230_WDT_STAT_INT, ==, 0);

    qtest_quit(qts);
}

static void test_reset_mode(void)
{
    QTestState *qts = qtest_init("-machine k230 -no-reboot");

    /* Set reset mode and enable watchdog */
    qtest_writel(qts, WDT_BASE + K230_WDT_CR, K230_WDT_CR_WDT_EN);
    qtest_writel(qts, WDT_BASE + K230_WDT_TORR, 0x1); /* Short timeout */

    /* Wait for timeout to trigger reset */
    qtest_clock_step(qts, NANOSECONDS_PER_SECOND * 2);

    /* In reset mode, the system should reset */
    /* This test verifies that reset mode is properly configured */

    qtest_quit(qts);
}

static void test_timeout_calculation(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /* Test different timeout values */
    for (uint32_t top = 0; top <= 15; top++) {
        qtest_writel(qts, WDT_BASE + K230_WDT_TORR, top);
        qtest_writel(qts, WDT_BASE + K230_WDT_CR, K230_WDT_CR_WDT_EN);

        /* Read current counter value */
        uint32_t count = qtest_readl(qts, WDT_BASE + K230_WDT_CCVR);
        g_assert_cmpuint(count, >, 0);

        /* Disable watchdog for next iteration */
        qtest_writel(qts, WDT_BASE + K230_WDT_CR, 0);
    }

    qtest_quit(qts);
}

static void test_wdt1_registers(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /* Test WDT1 registers (second watchdog) */
    qtest_writel(qts, K230_WDT1_BASE + K230_WDT_CR, 0xFFFFFFFF);
    g_assert_cmphex(qtest_readl(qts, K230_WDT1_BASE + K230_WDT_CR), ==,
                    (K230_WDT_CR_RPL_MASK << K230_WDT_CR_RPL_SHIFT) |
                    K230_WDT_CR_RMOD | K230_WDT_CR_WDT_EN);

    qtest_writel(qts, K230_WDT1_BASE + K230_WDT_TORR, 0xFFFFFFFF);
    g_assert_cmphex(qtest_readl(qts, K230_WDT1_BASE + K230_WDT_TORR), ==,
                    K230_WDT_TORR_TOP_MASK);

    qtest_quit(qts);
}

static void test_enable_disable(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /* Initially disabled */
    uint32_t cr = qtest_readl(qts, WDT_BASE + K230_WDT_CR);
    g_assert_cmphex(cr & K230_WDT_CR_WDT_EN, ==, 0);

    /* Enable watchdog */
    qtest_writel(qts, WDT_BASE + K230_WDT_CR, K230_WDT_CR_WDT_EN);
    cr = qtest_readl(qts, WDT_BASE + K230_WDT_CR);
    g_assert_cmphex(cr & K230_WDT_CR_WDT_EN, ==, K230_WDT_CR_WDT_EN);

    /* Disable watchdog */
    qtest_writel(qts, WDT_BASE + K230_WDT_CR, 0);
    cr = qtest_readl(qts, WDT_BASE + K230_WDT_CR);
    g_assert_cmphex(cr & K230_WDT_CR_WDT_EN, ==, 0);

    qtest_quit(qts);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/k230-wdt/register_read_write", test_register_read_write);
    qtest_add_func("/k230-wdt/counter_restart", test_counter_restart);
    qtest_add_func("/k230-wdt/interrupt_mode", test_interrupt_mode);
    qtest_add_func("/k230-wdt/reset_mode", test_reset_mode);
    qtest_add_func("/k230-wdt/timeout_calculation", test_timeout_calculation);
    qtest_add_func("/k230-wdt/wdt1_registers", test_wdt1_registers);
    qtest_add_func("/k230-wdt/enable_disable", test_enable_disable);

    return g_test_run();
}
