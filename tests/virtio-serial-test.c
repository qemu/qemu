/*
 * QTest testcase for VirtIO Serial
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"

/* Tests only initialization so far. TODO: Replace with functional tests */
static void pci_nop(void)
{
}

static void hotplug(void)
{
    qtest_qmp_device_add("virtserialport", "hp-port", NULL);

    qtest_qmp_device_del("hp-port");
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/virtio/serial/pci/nop", pci_nop);
    qtest_add_func("/virtio/serial/pci/hotplug", hotplug);

    qtest_start("-device virtio-serial-pci");
    ret = g_test_run();

    qtest_end();

    return ret;
}
