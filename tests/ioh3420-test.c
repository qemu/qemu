/*
 * QTest testcase for Intel X58 north bridge IOH
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib.h>
#include "libqtest.h"

/* Tests only initialization so far. TODO: Replace with functional tests */
static void nop(void)
{
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/ioh3420/nop", nop);

    qtest_start("-machine q35 -device ioh3420,bus=pcie.0,addr=1c.0,port=1,"
                "chassis=1,multifunction=on");
    ret = g_test_run();

    qtest_end();

    return ret;
}
