/*
 * QTest testcase for the CMSDK APB watchdog device
 *
 * Copyright (c) 2021 Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "libqtest-single.h"

/*
 * lm3s811evb watchdog; at board startup this runs at 200MHz / 16 == 12.5MHz,
 * which is 80ns per tick.
 */
#define WDOG_BASE 0x40000000

#define WDOGLOAD 0
#define WDOGVALUE 4
#define WDOGCONTROL 8
#define WDOGINTCLR 0xc
#define WDOGRIS 0x10
#define WDOGMIS 0x14
#define WDOGLOCK 0xc00

#define SSYS_BASE 0x400fe000
#define RCC 0x60
#define SYSDIV_SHIFT 23
#define SYSDIV_LENGTH 4

static void test_watchdog(void)
{
    g_assert_cmpuint(readl(WDOG_BASE + WDOGRIS), ==, 0);

    writel(WDOG_BASE + WDOGCONTROL, 1);
    writel(WDOG_BASE + WDOGLOAD, 1000);

    /* Step to just past the 500th tick */
    clock_step(500 * 80 + 1);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGRIS), ==, 0);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGVALUE), ==, 500);

    /* Just past the 1000th tick: timer should have fired */
    clock_step(500 * 80);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGRIS), ==, 1);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGVALUE), ==, 0);

    /* VALUE reloads at following tick */
    clock_step(80);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGVALUE), ==, 1000);

    /* Writing any value to WDOGINTCLR clears the interrupt and reloads */
    clock_step(500 * 80);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGVALUE), ==, 500);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGRIS), ==, 1);
    writel(WDOG_BASE + WDOGINTCLR, 0);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGVALUE), ==, 1000);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGRIS), ==, 0);
}

static void test_clock_change(void)
{
    uint32_t rcc;

    /*
     * Test that writing to the stellaris board's RCC register to
     * change the system clock frequency causes the watchdog
     * to change the speed it counts at.
     */
    g_assert_cmpuint(readl(WDOG_BASE + WDOGRIS), ==, 0);

    writel(WDOG_BASE + WDOGCONTROL, 1);
    writel(WDOG_BASE + WDOGLOAD, 1000);

    /* Step to just past the 500th tick */
    clock_step(80 * 500 + 1);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGRIS), ==, 0);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGVALUE), ==, 500);

    /* Rewrite RCC.SYSDIV from 16 to 8, so the clock is now 40ns per tick */
    rcc = readl(SSYS_BASE + RCC);
    g_assert_cmpuint(extract32(rcc, SYSDIV_SHIFT, SYSDIV_LENGTH), ==, 0xf);
    rcc = deposit32(rcc, SYSDIV_SHIFT, SYSDIV_LENGTH, 7);
    writel(SSYS_BASE + RCC, rcc);

    /* Just past the 1000th tick: timer should have fired */
    clock_step(40 * 500);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGRIS), ==, 1);

    g_assert_cmpuint(readl(WDOG_BASE + WDOGVALUE), ==, 0);

    /* VALUE reloads at following tick */
    clock_step(41);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGVALUE), ==, 1000);

    /* Writing any value to WDOGINTCLR clears the interrupt and reloads */
    clock_step(40 * 500);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGVALUE), ==, 500);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGRIS), ==, 1);
    writel(WDOG_BASE + WDOGINTCLR, 0);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGVALUE), ==, 1000);
    g_assert_cmpuint(readl(WDOG_BASE + WDOGRIS), ==, 0);
}

int main(int argc, char **argv)
{
    int r;

    g_test_init(&argc, &argv, NULL);

    qtest_start("-machine lm3s811evb");

    qtest_add_func("/cmsdk-apb-watchdog/watchdog", test_watchdog);
    qtest_add_func("/cmsdk-apb-watchdog/watchdog_clock_change",
                   test_clock_change);

    r = g_test_run();

    qtest_end();

    return r;
}
