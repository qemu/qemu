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
#include "exec/hwaddr.h"
#include "qemu/bitops.h"
#include "libqtest-single.h"

#define WDOG_BASE 0x40000000
#define WDOG_BASE_MPS2 0x40008000

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

#define WDOGLOAD_DEFAULT 0xFFFFFFFF
#define WDOGVALUE_DEFAULT 0xFFFFFFFF

typedef struct CMSDKAPBWatchdogTestArgs {
    int64_t tick;
    hwaddr wdog_base;
    const char *machine;
} CMSDKAPBWatchdogTestArgs;

enum {
    MACHINE_LM3S811EVB,
    MACHINE_MPS2_AN385,
};

/*
 * lm3s811evb watchdog; at board startup this runs at 200MHz / 16 == 12.5MHz,
 * which is 80ns per tick.
 *
 * IoTKit/ARMSSE dualtimer; driven at 25MHz in mps2-an385, so 40ns per tick
 */
static const CMSDKAPBWatchdogTestArgs machine_info[] = {
    [MACHINE_LM3S811EVB] = {
        .tick = 80,
        .wdog_base = WDOG_BASE,
        .machine = "lm3s811evb",
    },
    [MACHINE_MPS2_AN385] = {
        .tick = 40,
        .wdog_base = WDOG_BASE_MPS2,
        .machine = "mps2-an385",
    },
};

static void test_watchdog(const void *ptr)
{
    const CMSDKAPBWatchdogTestArgs *args = ptr;
    hwaddr wdog_base = args->wdog_base;
    int64_t tick = args->tick;
    g_autofree gchar *cmdline = g_strdup_printf("-machine %s", args->machine);
    qtest_start(cmdline);

    g_assert_cmpuint(readl(wdog_base + WDOGRIS), ==, 0);

    writel(wdog_base + WDOGCONTROL, 1);
    writel(wdog_base + WDOGLOAD, 1000);

    /* Step to just past the 500th tick */
    clock_step(500 * tick + 1);
    g_assert_cmpuint(readl(wdog_base + WDOGRIS), ==, 0);
    g_assert_cmpuint(readl(wdog_base + WDOGVALUE), ==, 500);

    /* Just past the 1000th tick: timer should have fired */
    clock_step(500 * tick);
    g_assert_cmpuint(readl(wdog_base + WDOGRIS), ==, 1);
    g_assert_cmpuint(readl(wdog_base + WDOGVALUE), ==, 0);

    /* VALUE reloads at following tick */
    clock_step(tick);
    g_assert_cmpuint(readl(wdog_base + WDOGVALUE), ==, 1000);

    /* Writing any value to WDOGINTCLR clears the interrupt and reloads */
    clock_step(500 * tick);
    g_assert_cmpuint(readl(wdog_base + WDOGVALUE), ==, 500);
    g_assert_cmpuint(readl(wdog_base + WDOGRIS), ==, 1);
    writel(wdog_base + WDOGINTCLR, 0);
    g_assert_cmpuint(readl(wdog_base + WDOGVALUE), ==, 1000);
    g_assert_cmpuint(readl(wdog_base + WDOGRIS), ==, 0);

    qtest_end();
}

/*
 * This test can only be executed in the stellaris board since it relies on a
 * component of the board to change the clocking parameters of the watchdog.
 */
static void test_clock_change(const void *ptr)
{
    uint32_t rcc;
    const CMSDKAPBWatchdogTestArgs *args = ptr;
    g_autofree gchar *cmdline = g_strdup_printf("-machine %s", args->machine);
    qtest_start(cmdline);

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
    g_assert_cmphex(extract32(rcc, SYSDIV_SHIFT, SYSDIV_LENGTH), ==, 0xf);
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

    qtest_end();
}

int main(int argc, char **argv)
{
    int r;

    g_test_init(&argc, &argv, NULL);

    if (qtest_has_machine(machine_info[MACHINE_LM3S811EVB].machine)) {
        qtest_add_data_func("/cmsdk-apb-watchdog/watchdog",
                            &machine_info[MACHINE_LM3S811EVB], test_watchdog);
        qtest_add_data_func("/cmsdk-apb-watchdog/watchdog_clock_change",
                            &machine_info[MACHINE_LM3S811EVB],
                            test_clock_change);
    }
    if (qtest_has_machine(machine_info[MACHINE_MPS2_AN385].machine)) {
        qtest_add_data_func("/cmsdk-apb-watchdog/watchdog_mps2",
                            &machine_info[MACHINE_MPS2_AN385], test_watchdog);
    }

    r = g_test_run();

    return r;
}
