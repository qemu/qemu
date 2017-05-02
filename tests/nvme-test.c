/*
 * QTest testcase for NVMe
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
    qtest_add_func("/nvme/nop", nop);

    qtest_start("-drive id=drv0,if=none,file=null-co://,format=raw "
                "-device nvme,drive=drv0,serial=foo");
    ret = g_test_run();

    qtest_end();

    return ret;
}
