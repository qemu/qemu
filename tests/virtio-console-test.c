/*
 * QTest testcase for VirtIO Console
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"

/* Tests only initialization so far. TODO: Replace with functional tests */
static void console_pci_nop(void)
{
    qtest_start("-device virtio-serial-pci,id=vser0 "
                "-device virtconsole,bus=vser0.0");
    qtest_end();
}

static void serialport_pci_nop(void)
{
    qtest_start("-device virtio-serial-pci,id=vser0 "
                "-device virtserialport,bus=vser0.0");
    qtest_end();
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/virtio/console/pci/nop", console_pci_nop);
    qtest_add_func("/virtio/serialport/pci/nop", serialport_pci_nop);

    ret = g_test_run();

    return ret;
}
