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

static void test_timer(void)
{
    /* Basic timer functionality test */

    reset_counter_and_timer();
    /*
     * The timer is behind a Peripheral Protection Controller, and
     * qtest accesses are always non-secure (no memory attributes),
     * so we must program the PPC to accept NS transactions.  TIMER0
     * is on port 0 of PPC0, controlled by bit 0 of this register.
     */
    writel(PERIPHNSPPC0, 1);
    /* We must enable the System Counter or the timer won't run. */
    writel(COUNTER_BASE + CNTCR, 1);

    /* Timer starts disabled and with a counter of 0 */
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_CTL), ==, 0);
    g_assert_cmpuint(readl(TIMER_BASE + CNTPCT_LO), ==, 0);
    g_assert_cmpuint(readl(TIMER_BASE + CNTPCT_HI), ==, 0);

    /* Turn it on */
    writel(TIMER_BASE + CNTP_CTL, 1);

    /* Is the timer ticking? */
    clock_step_ticks(100);
    g_assert_cmpuint(readl(TIMER_BASE + CNTPCT_LO), ==, 100);
    g_assert_cmpuint(readl(TIMER_BASE + CNTPCT_HI), ==, 0);

    /* Set the CompareValue to 4000 ticks */
    writel(TIMER_BASE + CNTP_CVAL_LO, 4000);
    writel(TIMER_BASE + CNTP_CVAL_HI, 0);

    /* Check TVAL view of the counter */
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_TVAL), ==, 3900);

    /* Advance to the CompareValue mark and check ISTATUS is set */
    clock_step_ticks(3900);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_TVAL), ==, 0);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_CTL), ==, 5);

    /* Now exercise the auto-reload part of the timer */
    writel(TIMER_BASE + CNTP_AIVAL_RELOAD, 200);
    writel(TIMER_BASE + CNTP_AIVAL_CTL, 1);

    /* Check AIVAL was reloaded and that ISTATUS is now clear */
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_AIVAL_LO), ==, 4200);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_AIVAL_HI), ==, 0);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_CTL), ==, 1);

    /*
     * Check that when we advance forward to the reload time the interrupt
     * fires and the value reloads
     */
    clock_step_ticks(100);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_CTL), ==, 1);
    clock_step_ticks(100);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_CTL), ==, 5);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_AIVAL_LO), ==, 4400);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_AIVAL_HI), ==, 0);

    clock_step_ticks(100);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_CTL), ==, 5);
    /* Check that writing 0 to CLR clears the interrupt */
    writel(TIMER_BASE + CNTP_AIVAL_CTL, 1);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_CTL), ==, 1);
    /* Check that when we move forward to the reload time it fires again */
    clock_step_ticks(100);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_CTL), ==, 5);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_AIVAL_LO), ==, 4600);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_AIVAL_HI), ==, 0);

    /*
     * Step the clock far enough that we overflow the low half of the
     * CNTPCT and AIVAL registers, and check that their high halves
     * give the right values. We do the forward movement in
     * non-autoinc mode because otherwise it takes forever as the
     * timer has to emulate all the 'reload at t + N, t + 2N, etc'
     * steps.
     */
    writel(TIMER_BASE + CNTP_AIVAL_CTL, 0);
    clock_step_ticks(0x42ULL << 32);
    g_assert_cmpuint(readl(TIMER_BASE + CNTPCT_LO), ==, 4400);
    g_assert_cmpuint(readl(TIMER_BASE + CNTPCT_HI), ==, 0x42);

    /* Turn on the autoinc again to check AIVAL_HI */
    writel(TIMER_BASE + CNTP_AIVAL_CTL, 1);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_AIVAL_LO), ==, 4600);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_AIVAL_HI), ==, 0x42);
}

static void test_timer_scale_change(void)
{
    /*
     * Test that the timer responds correctly to counter
     * scaling changes while it has an active timer.
     */
    reset_counter_and_timer();
    /* Give ourselves access to the timer, and enable the counter and timer */
    writel(PERIPHNSPPC0, 1);
    writel(COUNTER_BASE + CNTCR, 1);
    writel(TIMER_BASE + CNTP_CTL, 1);
    /* Set the CompareValue to 4000 ticks */
    writel(TIMER_BASE + CNTP_CVAL_LO, 4000);
    writel(TIMER_BASE + CNTP_CVAL_HI, 0);
    /* Advance halfway and check ISTATUS is not set */
    clock_step_ticks(2000);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_CTL), ==, 1);
    /* Reprogram the counter to run at 1/16th speed */
    writel(COUNTER_BASE + CNTCR, 0);
    writel(COUNTER_BASE + CNTSCR, 0x00100000); /* 1/16th normal speed */
    writel(COUNTER_BASE + CNTCR, 5); /* EN, SCEN */
    /* Advance to where the timer would have fired and check it has not */
    clock_step_ticks(2000);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_CTL), ==, 1);
    /* Advance to where the timer must fire at the new clock rate */
    clock_step_ticks(29996);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_CTL), ==, 1);
    clock_step_ticks(4);
    g_assert_cmpuint(readl(TIMER_BASE + CNTP_CTL), ==, 5);
}

int main(int argc, char **argv)
{
    int r;

    g_test_init(&argc, &argv, NULL);

    qtest_start("-machine mps3-an547");

    qtest_add_func("/sse-timer/counter", test_counter);
    qtest_add_func("/sse-timer/timer", test_timer);
    qtest_add_func("/sse-timer/timer-scale-change", test_timer_scale_change);

    r = g_test_run();

    qtest_end();

    return r;
}
