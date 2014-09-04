/*
 * QTest testcase for VirtIO Block Device
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
static void pci_nop(void)
{
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/virtio/blk/pci/nop", pci_nop);

    qtest_start("-drive id=drv0,if=none,file=/dev/null "
                "-device virtio-blk-pci,drive=drv0");
    ret = g_test_run();

    qtest_end();

    return ret;
}
