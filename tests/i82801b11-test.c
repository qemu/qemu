/*
 * QTest testcase for i82801b11
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

/* Tests only initialization so far. TODO: Replace with functional tests */
static void nop(void)
{
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/i82801b11/nop", nop);

    qtest_start("-machine q35 -device i82801b11-bridge,bus=pcie.0,addr=1e.0");
    ret = g_test_run();

    qtest_end();

    return ret;
}
