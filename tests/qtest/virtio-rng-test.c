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
#include "qemu/module.h"
#include "libqos/qgraph.h"
#include "libqos/virtio-rng.h"

#define PCI_SLOT_HP             0x06

static void rng_hotplug(void *obj, void *data, QGuestAllocator *alloc)
{
    QVirtioPCIDevice *dev = obj;
    QTestState *qts = dev->pdev->bus->qts;

    const char *arch = qtest_get_arch();

    qtest_qmp_device_add(qts, "virtio-rng-pci", "rng1",
                         "{'addr': %s}", stringify(PCI_SLOT_HP));

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        qpci_unplug_acpi_device_test(qts, "rng1", PCI_SLOT_HP);
    }
}

static void register_virtio_rng_test(void)
{
    qos_add_test("hotplug", "virtio-rng-pci", rng_hotplug, NULL);
}

libqos_init(register_virtio_rng_test);
