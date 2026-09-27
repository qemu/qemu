/*
 * QTest for Milk-V Duo Board
 *
 * Copyright (c) 2026 Kuan-Wei Chiu <visitorckw@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define CV1800B_CLK_BASE            0x03002000
#define CV1800B_CLK_BYPASS          0x030
#define CV1800B_CLK_BYPASS_RESET    0xFFFFFFFF
#define TEST_PATTERN_5A             0x5A5A5A5A
#define TEST_PATTERN_A5             0xA5A5A5A5

#define CV1800B_UART0_BASE          0x04140000
#define DW_UART_UCV                 0xF8
#define DW_UART_CTR                 0xFC
#define DW_UART_VERSION_3_23A       0x3332332A
#define DW_UART_TYPE_SIGNATURE      0x44570110

static void test_milkv_duo_uart(void)
{
    QTestState *qts;
    uint32_t component_version;
    uint32_t component_type;

    qts = qtest_init("-M milkv-duo");

    component_version = qtest_readl(qts, CV1800B_UART0_BASE + DW_UART_UCV);
    g_assert_cmphex(component_version, ==, DW_UART_VERSION_3_23A);

    component_type = qtest_readl(qts, CV1800B_UART0_BASE + DW_UART_CTR);
    g_assert_cmphex(component_type, ==, DW_UART_TYPE_SIGNATURE);

    qtest_quit(qts);
}

static void test_milkv_duo_clk(void)
{
    QTestState *qts;
    uint32_t clk_bypass_val;

    qts = qtest_init("-M milkv-duo");

    clk_bypass_val = qtest_readl(qts, CV1800B_CLK_BASE + CV1800B_CLK_BYPASS);
    g_assert_cmphex(clk_bypass_val, ==, CV1800B_CLK_BYPASS_RESET);

    qtest_writel(qts, CV1800B_CLK_BASE + CV1800B_CLK_BYPASS, TEST_PATTERN_5A);
    clk_bypass_val = qtest_readl(qts, CV1800B_CLK_BASE + CV1800B_CLK_BYPASS);
    g_assert_cmphex(clk_bypass_val, ==, TEST_PATTERN_5A);

    qtest_writel(qts, CV1800B_CLK_BASE + CV1800B_CLK_BYPASS, TEST_PATTERN_A5);
    clk_bypass_val = qtest_readl(qts, CV1800B_CLK_BASE + CV1800B_CLK_BYPASS);
    g_assert_cmphex(clk_bypass_val, ==, TEST_PATTERN_A5);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/riscv/milkv-duo/uart", test_milkv_duo_uart);
    qtest_add_func("/riscv/milkv-duo/clk", test_milkv_duo_clk);

    return g_test_run();
}
