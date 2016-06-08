/*
 * Copyright (C) 2016, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include <glib.h>

#define TEST_QHT_STRING "tests/qht-bench 1>/dev/null 2>&1 -R -S0.1 -D10000 -N1 "

static void test_qht(int n_threads, int update_rate, int duration)
{
    char *str;
    int rc;

    str = g_strdup_printf(TEST_QHT_STRING "-n %d -u %d -d %d",
                          n_threads, update_rate, duration);
    rc = system(str);
    g_free(str);
    g_assert_cmpint(rc, ==, 0);
}

static void test_2th0u1s(void)
{
    test_qht(2, 0, 1);
}

static void test_2th20u1s(void)
{
    test_qht(2, 20, 1);
}

static void test_2th0u5s(void)
{
    test_qht(2, 0, 5);
}

static void test_2th20u5s(void)
{
    test_qht(2, 20, 5);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    if (g_test_quick()) {
        g_test_add_func("/qht/parallel/2threads-0%updates-1s", test_2th0u1s);
        g_test_add_func("/qht/parallel/2threads-20%updates-1s", test_2th20u1s);
    } else {
        g_test_add_func("/qht/parallel/2threads-0%updates-5s", test_2th0u5s);
        g_test_add_func("/qht/parallel/2threads-20%updates-5s", test_2th20u5s);
    }
    return g_test_run();
}
