/*
 * QEMU ACPI PCI bridge
 *
 * Copyright (c) 2023 Red Hat, Inc.
 *
 * Author:
 *   Igor Mammedov <imammedo@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/acpi/pci.h"
#include "hw/pci/pci_bridge.h"
#include "hw/acpi/pcihp.h"

void build_pci_bridge_aml(AcpiDevAmlIf *adev, Aml *scope)
{
    PCIBridge *br = PCI_BRIDGE(adev);

    if (!DEVICE(br)->hotplugged) {
        PCIBus *sec_bus = pci_bridge_get_sec_bus(br);

        build_append_pci_bus_devices(scope, sec_bus);

        /*
         * generate hotplug slots descriptors if
         * bridge has ACPI PCI hotplug attached,
         */
        if (object_property_find(OBJECT(sec_bus), ACPI_PCIHP_PROP_BSEL)) {
            build_append_pcihp_slots(scope, sec_bus);
        }
    }
}
