/*
 * Timed average computation tests
 *
 * Copyright Nodalink, EURL. 2014
 *
 * Authors:
 *  Benoît Canet     <benoit.canet@nodalink.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qemu/timed-average.h"

/* This is the clock for QEMU_CLOCK_VIRTUAL */
static int64_t my_clock_value;

int64_t cpu_get_clock(void)
{
    return my_clock_value;
}

static void account(TimedAverage *ta)
{
    timed_average_account(ta, 1);
    timed_average_account(ta, 5);
    timed_average_account(ta, 2);
    timed_average_account(ta, 4);
    timed_average_account(ta, 3);
}

static void test_average(void)
{
    TimedAverage ta;
    uint64_t result;
    int i;

    /* we will compute some average on a period of 1 second */
    timed_average_init(&ta, QEMU_CLOCK_VIRTUAL, NANOSECONDS_PER_SECOND);

    result = timed_average_min(&ta);
    g_assert(result == 0);
    result = timed_average_avg(&ta);
    g_assert(result == 0);
    result = timed_average_max(&ta);
    g_assert(result == 0);

    for (i = 0; i < 100; i++) {
        account(&ta);
        result = timed_average_min(&ta);
        g_assert(result == 1);
        result = timed_average_avg(&ta);
        g_assert(result == 3);
        result = timed_average_max(&ta);
        g_assert(result == 5);
        my_clock_value += NANOSECONDS_PER_SECOND / 10;
    }

    my_clock_value += NANOSECONDS_PER_SECOND * 100;

    result = timed_average_min(&ta);
    g_assert(result == 0);
    result = timed_average_avg(&ta);
    g_assert(result == 0);
    result = timed_average_max(&ta);
    g_assert(result == 0);

    for (i = 0; i < 100; i++) {
        account(&ta);
        result = timed_average_min(&ta);
        g_assert(result == 1);
        result = timed_average_avg(&ta);
        g_assert(result == 3);
        result = timed_average_max(&ta);
        g_assert(result == 5);
        my_clock_value += NANOSECONDS_PER_SECOND / 10;
    }
}

int main(int argc, char **argv)
{
    /* tests in the same order as the header function declarations */
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/timed-average/average", test_average);
    return g_test_run();
}

