/*
 * QTest testcase for VirtIO NIC
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
#include "libqos/pci.h"

#define PCI_SLOT_HP             0x06

/* Tests only initialization so far. TODO: Replace with functional tests */
static void pci_nop(void)
{
}

static void hotplug(void)
{
    qpci_plug_device_test("virtio-net-pci", "net1", PCI_SLOT_HP, NULL);
    qpci_unplug_acpi_device_test("net1", PCI_SLOT_HP);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/virtio/net/pci/nop", pci_nop);
    qtest_add_func("/virtio/net/pci/hotplug", hotplug);

    qtest_start("-device virtio-net-pci");
    ret = g_test_run();

    qtest_end();

    return ret;
}
