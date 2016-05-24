/*
 * QTest testcase for IndustryPack Octal-RS232
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"

/* Tests only initialization so far. TODO: Replace with functional tests */
static void nop(void)
{
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/ipoctal232/tpci200/nop", nop);

    qtest_start("-device tpci200,id=ipack0 -device ipoctal232,bus=ipack0.0");
    ret = g_test_run();

    qtest_end();

    return ret;
}
