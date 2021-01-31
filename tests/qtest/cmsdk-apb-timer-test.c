/*
 * QTest testcase for the CMSDK APB timer device
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
#include "libqtest-single.h"

/* IoTKit/ARMSSE-200 timer0; driven at 25MHz in mps2-an385, so 40ns per tick */
#define TIMER_BASE 0x40000000

#define CTRL 0
#define VALUE 4
#define RELOAD 8
#define INTSTATUS 0xc

static void test_timer(void)
{
    g_assert_true(readl(TIMER_BASE + INTSTATUS) == 0);

    /* Start timer: will fire after 40 * 1000 == 40000 ns */
    writel(TIMER_BASE + RELOAD, 1000);
    writel(TIMER_BASE + CTRL, 9);

    /* Step to just past the 500th tick and check VALUE */
    clock_step(40 * 500 + 1);
    g_assert_cmpuint(readl(TIMER_BASE + INTSTATUS), ==, 0);
    g_assert_cmpuint(readl(TIMER_BASE + VALUE), ==, 500);

    /* Just past the 1000th tick: timer should have fired */
    clock_step(40 * 500);
    g_assert_cmpuint(readl(TIMER_BASE + INTSTATUS), ==, 1);
    g_assert_cmpuint(readl(TIMER_BASE + VALUE), ==, 0);

    /* VALUE reloads at the following tick */
    clock_step(40);
    g_assert_cmpuint(readl(TIMER_BASE + VALUE), ==, 1000);

    /* Check write-1-to-clear behaviour of INTSTATUS */
    writel(TIMER_BASE + INTSTATUS, 0);
    g_assert_cmpuint(readl(TIMER_BASE + INTSTATUS), ==, 1);
    writel(TIMER_BASE + INTSTATUS, 1);
    g_assert_cmpuint(readl(TIMER_BASE + INTSTATUS), ==, 0);

    /* Turn off the timer */
    writel(TIMER_BASE + CTRL, 0);
}

int main(int argc, char **argv)
{
    int r;

    g_test_init(&argc, &argv, NULL);

    qtest_start("-machine mps2-an385");

    qtest_add_func("/cmsdk-apb-timer/timer", test_timer);

    r = g_test_run();

    qtest_end();

    return r;
}
