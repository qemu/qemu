/*
 * Support for generating PCI related ACPI tables and passing them to Guests
 *
 * Copyright (C) 2006 Fabrice Bellard
 * Copyright (C) 2008-2010  Kevin O'Connor <kevin@koconnor.net>
 * Copyright (C) 2013-2019 Red Hat Inc
 * Copyright (C) 2019 Intel Corporation
 *
 * Author: Wei Yang <richardw.yang@linux.intel.com>
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_ACPI_PCI_H
#define HW_ACPI_PCI_H

#include "hw/acpi/bios-linker-loader.h"
#include "hw/acpi/acpi_aml_interface.h"

typedef struct AcpiMcfgInfo {
    uint64_t base;
    uint32_t size;
} AcpiMcfgInfo;

void build_mcfg(GArray *table_data, BIOSLinker *linker, AcpiMcfgInfo *info,
                const char *oem_id, const char *oem_table_id);
Aml *aml_pci_device_dsm(void);

void build_append_pci_bus_devices(Aml *parent_scope, PCIBus *bus);
void build_pci_bridge_aml(AcpiDevAmlIf *adev, Aml *scope);

void build_srat_generic_affinity_structures(GArray *table_data);

#endif
