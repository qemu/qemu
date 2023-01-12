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

    if (object_property_find(OBJECT(&br->sec_bus), ACPI_PCIHP_PROP_BSEL)) {
        build_append_pci_bus_devices(scope, pci_bridge_get_sec_bus(br));
    }
}
