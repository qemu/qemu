/*
 * QTest testcase for VirtIO RNG
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/pci.h"

#define PCI_SLOT_HP             0x06

/* Tests only initialization so far. TODO: Replace with functional tests */
static void pci_nop(void)
{
}

static void hotplug(void)
{
    const char *arch = qtest_get_arch();

    qtest_qmp_device_add("virtio-rng-pci", "rng1",
                         "{'addr': %s}", stringify(PCI_SLOT_HP));

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        qpci_unplug_acpi_device_test("rng1", PCI_SLOT_HP);
    }
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/virtio/rng/pci/nop", pci_nop);
    qtest_add_func("/virtio/rng/pci/hotplug", hotplug);

    qtest_start("-device virtio-rng-pci");
    ret = g_test_run();

    qtest_end();

    return ret;
}
