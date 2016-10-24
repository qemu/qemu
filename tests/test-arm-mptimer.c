/*
 * QTest testcase for the ARM MPTimer
 *
 * Copyright (c) 2016 Dmitry Osipenko <digetx@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "libqtest.h"

#define TIMER_BLOCK_SCALE(s)    ((((s) & 0xff) + 1) * 10)

#define TIMER_BLOCK_STEP(scaler, steps_nb) \
    clock_step(TIMER_BLOCK_SCALE(scaler) * (int64_t)(steps_nb) + 1)

#define TIMER_BASE_PHYS 0x1e000600

#define TIMER_LOAD      0x00
#define TIMER_COUNTER   0x04
#define TIMER_CONTROL   0x08
#define TIMER_INTSTAT   0x0C

#define TIMER_CONTROL_ENABLE        (1 << 0)
#define TIMER_CONTROL_PERIODIC      (1 << 1)
#define TIMER_CONTROL_IT_ENABLE     (1 << 2)
#define TIMER_CONTROL_PRESCALER(p)  (((p) & 0xff) << 8)

#define PERIODIC     1
#define ONESHOT      0
#define NOSCALE      0

static int nonscaled = NOSCALE;
static int scaled = 122;

static void timer_load(uint32_t load)
{
    writel(TIMER_BASE_PHYS + TIMER_LOAD, load);
}

static void timer_start(int periodic, uint32_t scale)
{
    uint32_t ctl = TIMER_CONTROL_ENABLE | TIMER_CONTROL_PRESCALER(scale);

    if (periodic) {
        ctl |= TIMER_CONTROL_PERIODIC;
    }

    writel(TIMER_BASE_PHYS + TIMER_CONTROL, ctl);
}

static void timer_stop(void)
{
    writel(TIMER_BASE_PHYS + TIMER_CONTROL, 0);
}

static void timer_int_clr(void)
{
    writel(TIMER_BASE_PHYS + TIMER_INTSTAT, 1);
}

static void timer_reset(void)
{
    timer_stop();
    timer_load(0);
    timer_int_clr();
}

static uint32_t timer_get_and_clr_int_sts(void)
{
    uint32_t int_sts = readl(TIMER_BASE_PHYS + TIMER_INTSTAT);

    if (int_sts) {
        timer_int_clr();
    }

    return int_sts;
}

static uint32_t timer_counter(void)
{
    return readl(TIMER_BASE_PHYS + TIMER_COUNTER);
}

static void timer_set_counter(uint32_t value)
{
    writel(TIMER_BASE_PHYS + TIMER_COUNTER, value);
}

static void test_timer_oneshot(gconstpointer arg)
{
    int scaler = *((int *) arg);

    timer_reset();
    timer_load(9999999);
    timer_start(ONESHOT, scaler);

    TIMER_BLOCK_STEP(scaler, 9999);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);
    g_assert_cmpuint(timer_counter(), ==, 9990000);

    TIMER_BLOCK_STEP(scaler, 9990000);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 1);

    TIMER_BLOCK_STEP(scaler, 9990000);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);
}

static void test_timer_pause(gconstpointer arg)
{
    int scaler = *((int *) arg);

    timer_reset();
    timer_load(999999999);
    timer_start(ONESHOT, scaler);

    TIMER_BLOCK_STEP(scaler, 999);

    g_assert_cmpuint(timer_counter(), ==, 999999000);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    TIMER_BLOCK_STEP(scaler, 9000);

    g_assert_cmpuint(timer_counter(), ==, 999990000);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    timer_stop();

    g_assert_cmpuint(timer_counter(), ==, 999990000);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    TIMER_BLOCK_STEP(scaler, 90000);

    g_assert_cmpuint(timer_counter(), ==, 999990000);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    timer_start(ONESHOT, scaler);

    TIMER_BLOCK_STEP(scaler, 999990000);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 1);
    g_assert_cmpuint(timer_counter(), ==, 0);

    TIMER_BLOCK_STEP(scaler, 999990000);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);
    g_assert_cmpuint(timer_counter(), ==, 0);
}

static void test_timer_reload(gconstpointer arg)
{
    int scaler = *((int *) arg);

    timer_reset();
    timer_load(UINT32_MAX);
    timer_start(ONESHOT, scaler);

    TIMER_BLOCK_STEP(scaler, 90000);

    g_assert_cmpuint(timer_counter(), ==, UINT32_MAX - 90000);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    timer_load(UINT32_MAX);

    TIMER_BLOCK_STEP(scaler, 90000);

    g_assert_cmpuint(timer_counter(), ==, UINT32_MAX - 90000);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);
}

static void test_timer_periodic(gconstpointer arg)
{
    int scaler = *((int *) arg);
    int repeat = 10;

    timer_reset();
    timer_load(100);
    timer_start(PERIODIC, scaler);

    while (repeat--) {
        clock_step(TIMER_BLOCK_SCALE(scaler) * (101 + repeat) + 1);

        g_assert_cmpuint(timer_counter(), ==, 100 - repeat);
        g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 1);

        clock_step(TIMER_BLOCK_SCALE(scaler) * (101 - repeat) - 1);
    }
}

static void test_timer_oneshot_to_periodic(gconstpointer arg)
{
    int scaler = *((int *) arg);

    timer_reset();
    timer_load(10000);
    timer_start(ONESHOT, scaler);

    TIMER_BLOCK_STEP(scaler, 1000);

    g_assert_cmpuint(timer_counter(), ==, 9000);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    timer_start(PERIODIC, scaler);

    TIMER_BLOCK_STEP(scaler, 14001);

    g_assert_cmpuint(timer_counter(), ==, 5000);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 1);
}

static void test_timer_periodic_to_oneshot(gconstpointer arg)
{
    int scaler = *((int *) arg);

    timer_reset();
    timer_load(99999999);
    timer_start(PERIODIC, scaler);

    TIMER_BLOCK_STEP(scaler, 999);

    g_assert_cmpuint(timer_counter(), ==, 99999000);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    timer_start(ONESHOT, scaler);

    TIMER_BLOCK_STEP(scaler, 99999009);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 1);
}

static void test_timer_prescaler(void)
{
    timer_reset();
    timer_load(9999999);
    timer_start(ONESHOT, NOSCALE);

    TIMER_BLOCK_STEP(NOSCALE, 9999998);

    g_assert_cmpuint(timer_counter(), ==, 1);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    TIMER_BLOCK_STEP(NOSCALE, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 1);

    timer_reset();
    timer_load(9999999);
    timer_start(ONESHOT, 0xAB);

    TIMER_BLOCK_STEP(0xAB, 9999998);

    g_assert_cmpuint(timer_counter(), ==, 1);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    TIMER_BLOCK_STEP(0xAB, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 1);
}

static void test_timer_prescaler_on_the_fly(void)
{
    timer_reset();
    timer_load(9999999);
    timer_start(ONESHOT, NOSCALE);

    TIMER_BLOCK_STEP(NOSCALE, 999);

    g_assert_cmpuint(timer_counter(), ==, 9999000);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    timer_start(ONESHOT, 0xAB);

    TIMER_BLOCK_STEP(0xAB, 9000);

    g_assert_cmpuint(timer_counter(), ==, 9990000);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);
}

static void test_timer_set_oneshot_counter_to_0(gconstpointer arg)
{
    int scaler = *((int *) arg);

    timer_reset();
    timer_load(UINT32_MAX);
    timer_start(ONESHOT, scaler);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_counter(), ==, UINT32_MAX - 1);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    timer_set_counter(0);

    TIMER_BLOCK_STEP(scaler, 10);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);
}

static void test_timer_set_periodic_counter_to_0(gconstpointer arg)
{
    int scaler = *((int *) arg);

    timer_reset();
    timer_load(UINT32_MAX);
    timer_start(PERIODIC, scaler);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_counter(), ==, UINT32_MAX - 1);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    timer_set_counter(0);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_counter(), ==, UINT32_MAX - (scaler ? 0 : 1));
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);

    timer_reset();
    timer_set_counter(UINT32_MAX);
    timer_start(PERIODIC, scaler);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_counter(), ==, UINT32_MAX - 1);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    timer_set_counter(0);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);
}

static void test_timer_noload_oneshot(gconstpointer arg)
{
    int scaler = *((int *) arg);

    timer_reset();
    timer_start(ONESHOT, scaler);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);
}

static void test_timer_noload_periodic(gconstpointer arg)
{
    int scaler = *((int *) arg);

    timer_reset();
    timer_start(PERIODIC, scaler);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);
}

static void test_timer_zero_load_oneshot(gconstpointer arg)
{
    int scaler = *((int *) arg);

    timer_reset();
    timer_start(ONESHOT, scaler);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);
    g_assert_cmpuint(timer_counter(), ==, 0);

    timer_load(0);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);
}

static void test_timer_zero_load_periodic(gconstpointer arg)
{
    int scaler = *((int *) arg);

    timer_reset();
    timer_start(PERIODIC, scaler);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);
    g_assert_cmpuint(timer_counter(), ==, 0);

    timer_load(0);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);
}

static void test_timer_zero_load_oneshot_to_nonzero(gconstpointer arg)
{
    int scaler = *((int *) arg);

    timer_reset();
    timer_start(ONESHOT, scaler);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);

    timer_load(0);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);
    g_assert_cmpuint(timer_counter(), ==, 0);

    timer_load(999);

    TIMER_BLOCK_STEP(scaler, 1001);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 1);
}

static void test_timer_zero_load_periodic_to_nonzero(gconstpointer arg)
{
    int scaler = *((int *) arg);
    int i;

    timer_reset();
    timer_start(PERIODIC, scaler);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);
    g_assert_cmpuint(timer_counter(), ==, 0);

    timer_load(0);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);

    timer_load(1999999);

    for (i = 1; i < 10; i++) {
        TIMER_BLOCK_STEP(scaler, 2000001);

        g_assert_cmpuint(timer_counter(), ==, 1999999 - i);
        g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 1);
        g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);
    }
}

static void test_timer_nonzero_load_oneshot_to_zero(gconstpointer arg)
{
    int scaler = *((int *) arg);

    timer_reset();
    timer_start(ONESHOT, scaler);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);
    g_assert_cmpuint(timer_counter(), ==, 0);

    timer_load(UINT32_MAX);
    timer_load(0);

    TIMER_BLOCK_STEP(scaler, 100);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);
}

static void test_timer_nonzero_load_periodic_to_zero(gconstpointer arg)
{
    int scaler = *((int *) arg);

    timer_reset();
    timer_start(PERIODIC, scaler);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);

    timer_load(UINT32_MAX);
    timer_load(0);

    TIMER_BLOCK_STEP(scaler, 100);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);
}

static void test_timer_set_periodic_counter_on_the_fly(gconstpointer arg)
{
    int scaler = *((int *) arg);

    timer_reset();
    timer_load(UINT32_MAX / 2);
    timer_start(PERIODIC, scaler);

    TIMER_BLOCK_STEP(scaler, 100);

    g_assert_cmpuint(timer_counter(), ==, UINT32_MAX / 2 - 100);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    timer_set_counter(UINT32_MAX);

    TIMER_BLOCK_STEP(scaler, 100);

    g_assert_cmpuint(timer_counter(), ==, UINT32_MAX - 100);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);
}

static void test_timer_enable_and_set_counter(gconstpointer arg)
{
    int scaler = *((int *) arg);

    timer_reset();
    timer_start(ONESHOT, scaler);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);

    timer_set_counter(UINT32_MAX);

    TIMER_BLOCK_STEP(scaler, 100);

    g_assert_cmpuint(timer_counter(), ==, UINT32_MAX - 100);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);
}

static void test_timer_set_counter_and_enable(gconstpointer arg)
{
    int scaler = *((int *) arg);

    timer_reset();
    timer_set_counter(UINT32_MAX);
    timer_start(ONESHOT, scaler);

    TIMER_BLOCK_STEP(scaler, 100);

    g_assert_cmpuint(timer_counter(), ==, UINT32_MAX - 100);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);
}

static void test_timer_set_counter_disabled(void)
{
    timer_reset();
    timer_set_counter(999999999);

    TIMER_BLOCK_STEP(NOSCALE, 100);

    g_assert_cmpuint(timer_counter(), ==, 999999999);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);
}

static void test_timer_load_disabled(void)
{
    timer_reset();
    timer_load(999999999);

    TIMER_BLOCK_STEP(NOSCALE, 100);

    g_assert_cmpuint(timer_counter(), ==, 999999999);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);
}

static void test_timer_oneshot_with_counter_0_on_start(gconstpointer arg)
{
    int scaler = *((int *) arg);

    timer_reset();
    timer_load(999);
    timer_set_counter(0);
    timer_start(ONESHOT, scaler);

    TIMER_BLOCK_STEP(scaler, 100);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);

    TIMER_BLOCK_STEP(scaler, 100);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);
}

static void test_timer_periodic_with_counter_0_on_start(gconstpointer arg)
{
    int scaler = *((int *) arg);
    int i;

    timer_reset();
    timer_load(UINT32_MAX);
    timer_set_counter(0);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);
    g_assert_cmpuint(timer_counter(), ==, 0);

    timer_start(PERIODIC, scaler);

    TIMER_BLOCK_STEP(scaler, 100);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);
    g_assert_cmpuint(timer_counter(), ==, UINT32_MAX + (scaler ? 1 : 0) - 100);

    TIMER_BLOCK_STEP(scaler, 100);

    g_assert_cmpuint(timer_counter(), ==, UINT32_MAX + (scaler ? 1 : 0) - 200);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    timer_reset();
    timer_load(1999999);
    timer_set_counter(0);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    timer_start(PERIODIC, scaler);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);

    for (i = 2 - (!!scaler ? 1 : 0); i < 10; i++) {
        TIMER_BLOCK_STEP(scaler, 2000001);

        g_assert_cmpuint(timer_counter(), ==, 1999999 - i);
        g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 1);
        g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);
    }
}

static void test_periodic_counter(gconstpointer arg)
{
    const int test_load = 10;
    int scaler = *((int *) arg);
    int test_val;

    timer_reset();
    timer_load(test_load);
    timer_start(PERIODIC, scaler);

    clock_step(1);

    for (test_val = 0; test_val <= test_load; test_val++) {
        clock_step(TIMER_BLOCK_SCALE(scaler) * test_load);
        g_assert_cmpint(timer_counter(), ==, test_val);
    }
}

static void test_timer_set_counter_periodic_with_zero_load(gconstpointer arg)
{
    int scaler = *((int *) arg);

    timer_reset();
    timer_start(PERIODIC, scaler);
    timer_load(0);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);

    timer_set_counter(999);

    TIMER_BLOCK_STEP(scaler, 999);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 1);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);
}

static void test_timer_set_oneshot_load_to_0(gconstpointer arg)
{
    int scaler = *((int *) arg);

    timer_reset();
    timer_load(UINT32_MAX);
    timer_start(ONESHOT, scaler);

    TIMER_BLOCK_STEP(scaler, 100);

    g_assert_cmpuint(timer_counter(), ==, UINT32_MAX - 100);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    timer_load(0);

    TIMER_BLOCK_STEP(scaler, 100);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);

    TIMER_BLOCK_STEP(scaler, 100);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);
}

static void test_timer_set_periodic_load_to_0(gconstpointer arg)
{
    int scaler = *((int *) arg);

    timer_reset();
    timer_load(UINT32_MAX);
    timer_start(PERIODIC, scaler);

    TIMER_BLOCK_STEP(scaler, 100);

    g_assert_cmpuint(timer_counter(), ==, UINT32_MAX - 100);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    timer_load(0);

    TIMER_BLOCK_STEP(scaler, 100);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);

    TIMER_BLOCK_STEP(scaler, 100);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);
    g_assert_cmpuint(timer_counter(), ==, 0);
}

static void test_deferred_trigger(void)
{
    int mode = ONESHOT;

again:
    timer_reset();
    timer_start(mode, 255);

    clock_step(100);

    g_assert_cmpuint(timer_counter(), ==, 0);

    TIMER_BLOCK_STEP(255, 1);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 1);

    timer_reset();
    timer_load(2);
    timer_start(mode, 255);

    clock_step(100);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    TIMER_BLOCK_STEP(255, 1);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    TIMER_BLOCK_STEP(255, 1);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 1);

    timer_reset();
    timer_load(UINT32_MAX);
    timer_start(mode, 255);

    clock_step(100);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    timer_set_counter(0);

    clock_step(100);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    TIMER_BLOCK_STEP(255, 1);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 1);

    timer_reset();
    timer_load(UINT32_MAX);
    timer_start(mode, 255);

    clock_step(100);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    timer_load(0);

    clock_step(100);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    TIMER_BLOCK_STEP(255, 1);

    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 1);

    if (mode == ONESHOT) {
        mode = PERIODIC;
        goto again;
    }
}

static void test_timer_zero_load_mode_switch(gconstpointer arg)
{
    int scaler = *((int *) arg);

    timer_reset();
    timer_load(0);
    timer_start(PERIODIC, scaler);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);

    TIMER_BLOCK_STEP(scaler, 1);

    timer_start(ONESHOT, scaler);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    TIMER_BLOCK_STEP(scaler, 1);

    timer_start(PERIODIC, scaler);

    TIMER_BLOCK_STEP(scaler, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, !!scaler);
}

static void test_timer_zero_load_prescaled_periodic_to_nonscaled_oneshot(void)
{
    timer_reset();
    timer_load(0);
    timer_start(PERIODIC, 255);

    TIMER_BLOCK_STEP(255, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 1);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    TIMER_BLOCK_STEP(255, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 1);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    TIMER_BLOCK_STEP(255, 1);

    timer_start(ONESHOT, NOSCALE);

    TIMER_BLOCK_STEP(NOSCALE, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 1);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    TIMER_BLOCK_STEP(NOSCALE, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);
}

static void test_timer_zero_load_prescaled_oneshot_to_nonscaled_periodic(void)
{
    timer_reset();
    timer_load(0);
    timer_start(ONESHOT, 255);

    TIMER_BLOCK_STEP(255, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 1);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    timer_start(PERIODIC, NOSCALE);

    TIMER_BLOCK_STEP(NOSCALE, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);
}

static void test_timer_zero_load_nonscaled_oneshot_to_prescaled_periodic(void)
{
    timer_reset();
    timer_load(0);
    timer_start(ONESHOT, NOSCALE);

    TIMER_BLOCK_STEP(NOSCALE, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    timer_start(PERIODIC, 255);

    TIMER_BLOCK_STEP(255, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 1);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    TIMER_BLOCK_STEP(255, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 1);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);
}

static void test_timer_zero_load_nonscaled_periodic_to_prescaled_oneshot(void)
{
    timer_reset();
    timer_load(0);
    timer_start(PERIODIC, NOSCALE);

    TIMER_BLOCK_STEP(NOSCALE, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    timer_start(ONESHOT, 255);

    TIMER_BLOCK_STEP(255, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 1);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);

    TIMER_BLOCK_STEP(255, 1);

    g_assert_cmpuint(timer_counter(), ==, 0);
    g_assert_cmpuint(timer_get_and_clr_int_sts(), ==, 0);
}

int main(int argc, char **argv)
{
    int *scaler = &nonscaled;
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("mptimer/deferred_trigger", test_deferred_trigger);
    qtest_add_func("mptimer/load_disabled", test_timer_load_disabled);
    qtest_add_func("mptimer/set_counter_disabled", test_timer_set_counter_disabled);
    qtest_add_func("mptimer/zero_load_prescaled_periodic_to_nonscaled_oneshot",
                   test_timer_zero_load_prescaled_periodic_to_nonscaled_oneshot);
    qtest_add_func("mptimer/zero_load_prescaled_oneshot_to_nonscaled_periodic",
                   test_timer_zero_load_prescaled_oneshot_to_nonscaled_periodic);
    qtest_add_func("mptimer/zero_load_nonscaled_oneshot_to_prescaled_periodic",
                   test_timer_zero_load_nonscaled_oneshot_to_prescaled_periodic);
    qtest_add_func("mptimer/zero_load_nonscaled_periodic_to_prescaled_oneshot",
                   test_timer_zero_load_nonscaled_periodic_to_prescaled_oneshot);
    qtest_add_func("mptimer/prescaler", test_timer_prescaler);
    qtest_add_func("mptimer/prescaler_on_the_fly", test_timer_prescaler_on_the_fly);

tests_with_prescaler_arg:
    qtest_add_data_func(
        g_strdup_printf("mptimer/oneshot scaler=%d", *scaler),
                        scaler, test_timer_oneshot);
    qtest_add_data_func(
        g_strdup_printf("mptimer/pause scaler=%d", *scaler),
                        scaler, test_timer_pause);
    qtest_add_data_func(
        g_strdup_printf("mptimer/reload scaler=%d", *scaler),
                        scaler, test_timer_reload);
    qtest_add_data_func(
        g_strdup_printf("mptimer/periodic scaler=%d", *scaler),
                        scaler, test_timer_periodic);
    qtest_add_data_func(
        g_strdup_printf("mptimer/oneshot_to_periodic scaler=%d", *scaler),
                        scaler, test_timer_oneshot_to_periodic);
    qtest_add_data_func(
        g_strdup_printf("mptimer/periodic_to_oneshot scaler=%d", *scaler),
                        scaler, test_timer_periodic_to_oneshot);
    qtest_add_data_func(
        g_strdup_printf("mptimer/set_oneshot_counter_to_0 scaler=%d", *scaler),
                        scaler, test_timer_set_oneshot_counter_to_0);
    qtest_add_data_func(
        g_strdup_printf("mptimer/set_periodic_counter_to_0 scaler=%d", *scaler),
                        scaler, test_timer_set_periodic_counter_to_0);
    qtest_add_data_func(
        g_strdup_printf("mptimer/noload_oneshot scaler=%d", *scaler),
                        scaler, test_timer_noload_oneshot);
    qtest_add_data_func(
        g_strdup_printf("mptimer/noload_periodic scaler=%d", *scaler),
                        scaler, test_timer_noload_periodic);
    qtest_add_data_func(
        g_strdup_printf("mptimer/zero_load_oneshot scaler=%d", *scaler),
                        scaler, test_timer_zero_load_oneshot);
    qtest_add_data_func(
        g_strdup_printf("mptimer/zero_load_periodic scaler=%d", *scaler),
                        scaler, test_timer_zero_load_periodic);
    qtest_add_data_func(
        g_strdup_printf("mptimer/zero_load_oneshot_to_nonzero scaler=%d", *scaler),
                        scaler, test_timer_zero_load_oneshot_to_nonzero);
    qtest_add_data_func(
        g_strdup_printf("mptimer/zero_load_periodic_to_nonzero scaler=%d", *scaler),
                        scaler, test_timer_zero_load_periodic_to_nonzero);
    qtest_add_data_func(
        g_strdup_printf("mptimer/nonzero_load_oneshot_to_zero scaler=%d", *scaler),
                        scaler, test_timer_nonzero_load_oneshot_to_zero);
    qtest_add_data_func(
        g_strdup_printf("mptimer/nonzero_load_periodic_to_zero scaler=%d", *scaler),
                        scaler, test_timer_nonzero_load_periodic_to_zero);
    qtest_add_data_func(
        g_strdup_printf("mptimer/set_periodic_counter_on_the_fly scaler=%d", *scaler),
                        scaler, test_timer_set_periodic_counter_on_the_fly);
    qtest_add_data_func(
        g_strdup_printf("mptimer/enable_and_set_counter scaler=%d", *scaler),
                        scaler, test_timer_enable_and_set_counter);
    qtest_add_data_func(
        g_strdup_printf("mptimer/set_counter_and_enable scaler=%d", *scaler),
                        scaler, test_timer_set_counter_and_enable);
    qtest_add_data_func(
        g_strdup_printf("mptimer/oneshot_with_counter_0_on_start scaler=%d", *scaler),
                        scaler, test_timer_oneshot_with_counter_0_on_start);
    qtest_add_data_func(
        g_strdup_printf("mptimer/periodic_with_counter_0_on_start scaler=%d", *scaler),
                        scaler, test_timer_periodic_with_counter_0_on_start);
    qtest_add_data_func(
        g_strdup_printf("mptimer/periodic_counter scaler=%d", *scaler),
                        scaler, test_periodic_counter);
    qtest_add_data_func(
        g_strdup_printf("mptimer/set_counter_periodic_with_zero_load scaler=%d", *scaler),
                        scaler, test_timer_set_counter_periodic_with_zero_load);
    qtest_add_data_func(
        g_strdup_printf("mptimer/set_oneshot_load_to_0 scaler=%d", *scaler),
                        scaler, test_timer_set_oneshot_load_to_0);
    qtest_add_data_func(
        g_strdup_printf("mptimer/set_periodic_load_to_0 scaler=%d", *scaler),
                        scaler, test_timer_set_periodic_load_to_0);
    qtest_add_data_func(
        g_strdup_printf("mptimer/zero_load_mode_switch scaler=%d", *scaler),
                        scaler, test_timer_zero_load_mode_switch);

    if (scaler == &nonscaled) {
        scaler = &scaled;
        goto tests_with_prescaler_arg;
    }

    qtest_start("-machine vexpress-a9");
    ret = g_test_run();
    qtest_end();

    return ret;
}
