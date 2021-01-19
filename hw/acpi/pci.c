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

#include "qemu/osdep.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/pci.h"
#include "hw/pci/pcie_host.h"

void build_mcfg(GArray *table_data, BIOSLinker *linker, AcpiMcfgInfo *info,
                const char *oem_id, const char *oem_table_id)
{
    int mcfg_start = table_data->len;

    /*
     * PCI Firmware Specification, Revision 3.0
     * 4.1.2 MCFG Table Description.
     */
    acpi_data_push(table_data, sizeof(AcpiTableHeader));
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 8);

    /*
     * Memory Mapped Enhanced Configuration Space Base Address Allocation
     * Structure
     */
    /* Base address, processor-relative */
    build_append_int_noprefix(table_data, info->base, 8);
    /* PCI segment group number */
    build_append_int_noprefix(table_data, 0, 2);
    /* Starting PCI Bus number */
    build_append_int_noprefix(table_data, 0, 1);
    /* Final PCI Bus number */
    build_append_int_noprefix(table_data, PCIE_MMCFG_BUS(info->size - 1), 1);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 4);

    build_header(linker, table_data, (void *)(table_data->data + mcfg_start),
                 "MCFG", table_data->len - mcfg_start, 1, oem_id, oem_table_id);
}

