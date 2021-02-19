/*
 * QTest testcase for the SSE timer device
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

/*
 * SSE-123/SSE-300 timer in the mps3-an547 board, where it is driven
 * at 32MHz, so 31.25ns per tick.
 */
#define TIMER_BASE 0x48000000

/* PERIPHNSPPC0 register in the SSE-300 Secure Access Configuration block */
#define PERIPHNSPPC0 (0x50080000 + 0x70)

/* Base of the System Counter control frame */
#define COUNTER_BASE 0x58100000

/* SSE counter register offsets in the control frame */
#define CNTCR 0
#define CNTSR 0x4
#define CNTCV_LO 0x8
#define CNTCV_HI 0xc
#define CNTSCR 0x10

/* SSE timer register offsets */
#define CNTPCT_LO 0
#define CNTPCT_HI 4
#define CNTFRQ 0x10
#define CNTP_CVAL_LO 0x20
#define CNTP_CVAL_HI 0x24
#define CNTP_TVAL 0x28
#define CNTP_CTL 0x2c
#define CNTP_AIVAL_LO 0x40
#define CNTP_AIVAL_HI 0x44
#define CNTP_AIVAL_RELOAD 0x48
#define CNTP_AIVAL_CTL 0x4c

/* 4 ticks in nanoseconds (so we can work in integers) */
#define FOUR_TICKS 125

static void clock_step_ticks(uint64_t ticks)
{
    /*
     * Advance the qtest clock by however many nanoseconds we
     * need to move the timer forward the specified number of ticks.
     * ticks must be a multiple of 4, so we get a whole number of ns.
     */
    assert(!(ticks & 3));
    clock_step(FOUR_TICKS * (ticks >> 2));
}

static void reset_counter_and_timer(void)
{
    /*
     * Reset the system counter and the timer between tests. This
     * isn't a full reset, but it's sufficient for what the tests check.
     */
    writel(COUNTER_BASE + CNTCR, 0);
    writel(TIMER_BASE + CNTP_CTL, 0);
    writel(TIMER_BASE + CNTP_AIVAL_CTL, 0);
    writel(COUNTER_BASE + CNTCV_LO, 0);
    writel(COUNTER_BASE + CNTCV_HI, 0);
}

static void test_counter(void)
{
    /* Basic counter functionality test */

    reset_counter_and_timer();
    /* The counter should start disabled: check that it doesn't move */
    clock_step_ticks(100);
    g_assert_cmpuint(readl(COUNTER_BASE + CNTCV_LO), ==, 0);
    g_assert_cmpuint(readl(COUNTER_BASE + CNTCV_HI), ==, 0);
    /* Now enable it and check that it does count */
    writel(COUNTER_BASE + CNTCR, 1);
    clock_step_ticks(100);
    g_assert_cmpuint(readl(COUNTER_BASE + CNTCV_LO), ==, 100);
    g_assert_cmpuint(readl(COUNTER_BASE + CNTCV_HI), ==, 0);
    /* Check the counter scaling functionality */
    writel(COUNTER_BASE + CNTCR, 0);
    writel(COUNTER_BASE + CNTSCR, 0x00100000); /* 1/16th normal speed */
    writel(COUNTER_BASE + CNTCR, 5); /* EN, SCEN */
    clock_step_ticks(160);
    g_assert_cmpuint(readl(COUNTER_BASE + CNTCV_LO), ==, 110);
    g_assert_cmpuint(readl(COUNTER_BASE + CNTCV_HI), ==, 0);
}

int main(int argc, char **argv)
{
    int r;

    g_test_init(&argc, &argv, NULL);

    qtest_start("-machine mps3-an547");

    qtest_add_func("/sse-timer/counter", test_counter);

    r = g_test_run();

    qtest_end();

    return r;
}
