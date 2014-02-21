/*
 * QTest testcase for PV Panic
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>
#include <string.h>
#include "libqtest.h"
#include "qemu/osdep.h"

static void test_panic(void)
{
    uint8_t val;

    val = inb(0x505);
    g_assert_cmpuint(val, ==, 1);

    outb(0x505, 0x1);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/pvpanic/panic", test_panic);

    qtest_start("-device pvpanic");
    ret = g_test_run();

    qtest_end();

    return ret;
}
