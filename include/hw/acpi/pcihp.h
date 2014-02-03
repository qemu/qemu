/*
 * QEMU<->ACPI BIOS PCI hotplug interface
 *
 * QEMU supports PCI hotplug via ACPI. This module
 * implements the interface between QEMU and the ACPI BIOS.
 * Interface specification - see docs/specs/acpi_pci_hotplug.txt
 *
 * Copyright (c) 2013, Red Hat Inc, Michael S. Tsirkin (mst@redhat.com)
 * Copyright (c) 2006 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#ifndef HW_ACPI_PCIHP_H
#define HW_ACPI_PCIHP_H

#include <inttypes.h>
#include <qemu/typedefs.h>
#include "hw/pci/pci.h" /* for PCIHotplugState */

typedef struct AcpiPciHpPciStatus {
    uint32_t up; /* deprecated, maintained for migration compatibility */
    uint32_t down;
    uint32_t hotplug_enable;
    uint32_t device_present;
} AcpiPciHpPciStatus;

#define ACPI_PCIHP_PROP_BSEL "acpi-pcihp-bsel"
#define ACPI_PCIHP_MAX_HOTPLUG_BUS 256

typedef struct AcpiPciHpState {
    AcpiPciHpPciStatus acpi_pcihp_pci_status[ACPI_PCIHP_MAX_HOTPLUG_BUS];
    uint32_t hotplug_select;
    PCIBus *root;
    MemoryRegion io;
} AcpiPciHpState;

void acpi_pcihp_init(AcpiPciHpState *, PCIBus *root,
                     MemoryRegion *address_space_io);

/* Invoke on device hotplug */
int acpi_pcihp_device_hotplug(AcpiPciHpState *, PCIDevice *,
                              PCIHotplugState state);

/* Called on reset */
void acpi_pcihp_reset(AcpiPciHpState *s);

extern const VMStateDescription vmstate_acpi_pcihp_pci_status;

#define VMSTATE_PCI_HOTPLUG(pcihp, state, test_pcihp) \
        VMSTATE_UINT32_TEST(pcihp.hotplug_select, state, \
                            test_pcihp), \
        VMSTATE_STRUCT_ARRAY_TEST(pcihp.acpi_pcihp_pci_status, state, \
                                  ACPI_PCIHP_MAX_HOTPLUG_BUS, \
                                  test_pcihp, 1, \
                                  vmstate_acpi_pcihp_pci_status, \
                                  AcpiPciHpPciStatus)

#endif
