/* Support for generating ACPI tables and passing them to Guests
 *
 * Copyright (C) 2008-2010  Kevin O'Connor <kevin@koconnor.net>
 * Copyright (C) 2006 Fabrice Bellard
 * Copyright (C) 2013 Red Hat Inc
 *
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
#include "qapi/error.h"

#include "exec/memory.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/utils.h"
#include "hw/i386/pc.h"
#include "target/i386/cpu.h"

#include "acpi-build.h"
#include "acpi-common.h"

void pc_madt_cpu_entry(AcpiDeviceIf *adev, int uid,
                       const CPUArchIdList *apic_ids, GArray *entry,
                       bool force_enabled)
{
    uint32_t apic_id = apic_ids->cpus[uid].arch_id;
    /* Flags – Local APIC Flags */
    uint32_t flags = apic_ids->cpus[uid].cpu != NULL || force_enabled ?
                     1 /* Enabled */ : 0;

    /* ACPI spec says that LAPIC entry for non present
     * CPU may be omitted from MADT or it must be marked
     * as disabled. However omitting non present CPU from
     * MADT breaks hotplug on linux. So possible CPUs
     * should be put in MADT but kept disabled.
     */
    if (apic_id < 255) {
        /* Rev 1.0b, Table 5-13 Processor Local APIC Structure */
        build_append_int_noprefix(entry, 0, 1);       /* Type */
        build_append_int_noprefix(entry, 8, 1);       /* Length */
        build_append_int_noprefix(entry, uid, 1);     /* ACPI Processor ID */
        build_append_int_noprefix(entry, apic_id, 1); /* APIC ID */
        build_append_int_noprefix(entry, flags, 4); /* Flags */
    } else {
        /* Rev 4.0, 5.2.12.12 Processor Local x2APIC Structure */
        build_append_int_noprefix(entry, 9, 1);       /* Type */
        build_append_int_noprefix(entry, 16, 1);      /* Length */
        build_append_int_noprefix(entry, 0, 2);       /* Reserved */
        build_append_int_noprefix(entry, apic_id, 4); /* X2APIC ID */
        build_append_int_noprefix(entry, flags, 4);   /* Flags */
        build_append_int_noprefix(entry, uid, 4);     /* ACPI Processor UID */
    }
}

static void build_ioapic(GArray *entry, uint8_t id, uint32_t addr, uint32_t irq)
{
    /* Rev 1.0b, 5.2.8.2 IO APIC */
    build_append_int_noprefix(entry, 1, 1);    /* Type */
    build_append_int_noprefix(entry, 12, 1);   /* Length */
    build_append_int_noprefix(entry, id, 1);   /* IO APIC ID */
    build_append_int_noprefix(entry, 0, 1);    /* Reserved */
    build_append_int_noprefix(entry, addr, 4); /* IO APIC Address */
    build_append_int_noprefix(entry, irq, 4);  /* System Vector Base */
}

static void
build_xrupt_override(GArray *entry, uint8_t src, uint32_t gsi, uint16_t flags)
{
    /* Rev 1.0b, 5.2.8.3.1 Interrupt Source Overrides */
    build_append_int_noprefix(entry, 2, 1);  /* Type */
    build_append_int_noprefix(entry, 10, 1); /* Length */
    build_append_int_noprefix(entry, 0, 1);  /* Bus */
    build_append_int_noprefix(entry, src, 1);  /* Source */
    /* Global System Interrupt Vector */
    build_append_int_noprefix(entry, gsi, 4);
    build_append_int_noprefix(entry, flags, 2);  /* Flags */
}

/*
 * ACPI spec, Revision 1.0b
 * 5.2.8 Multiple APIC Description Table
 */
void acpi_build_madt(GArray *table_data, BIOSLinker *linker,
                     X86MachineState *x86ms, AcpiDeviceIf *adev,
                     const char *oem_id, const char *oem_table_id)
{
    int i;
    bool x2apic_mode = false;
    MachineClass *mc = MACHINE_GET_CLASS(x86ms);
    const CPUArchIdList *apic_ids = mc->possible_cpu_arch_ids(MACHINE(x86ms));
    AcpiDeviceIfClass *adevc = ACPI_DEVICE_IF_GET_CLASS(adev);
    AcpiTable table = { .sig = "APIC", .rev = 1, .oem_id = oem_id,
                        .oem_table_id = oem_table_id };

    acpi_table_begin(&table, table_data);
    /* Local APIC Address */
    build_append_int_noprefix(table_data, APIC_DEFAULT_ADDRESS, 4);
    build_append_int_noprefix(table_data, 1 /* PCAT_COMPAT */, 4); /* Flags */

    for (i = 0; i < apic_ids->len; i++) {
        adevc->madt_cpu(adev, i, apic_ids, table_data, false);
        if (apic_ids->cpus[i].arch_id > 254) {
            x2apic_mode = true;
        }
    }

    build_ioapic(table_data, ACPI_BUILD_IOAPIC_ID, IO_APIC_DEFAULT_ADDRESS, 0);
    if (x86ms->ioapic2) {
        build_ioapic(table_data, ACPI_BUILD_IOAPIC_ID + 1,
                     IO_APIC_SECONDARY_ADDRESS, IO_APIC_SECONDARY_IRQBASE);
    }

    if (x86ms->apic_xrupt_override) {
        build_xrupt_override(table_data, 0, 2,
            0 /* Flags: Conforms to the specifications of the bus */);
    }

    for (i = 1; i < 16; i++) {
        if (!(x86ms->pci_irq_mask & (1 << i))) {
            /* No need for a INT source override structure. */
            continue;
        }
        build_xrupt_override(table_data, i, i,
            0xd /* Flags: Active high, Level Triggered */);
    }

    if (x2apic_mode) {
        /* Rev 4.0, 5.2.12.13 Local x2APIC NMI Structure*/
        build_append_int_noprefix(table_data, 0xA, 1); /* Type */
        build_append_int_noprefix(table_data, 12, 1);  /* Length */
        build_append_int_noprefix(table_data, 0, 2);   /* Flags */
        /* ACPI Processor UID */
        build_append_int_noprefix(table_data, 0xFFFFFFFF /* all processors */,
                                  4);
        /* Local x2APIC LINT# */
        build_append_int_noprefix(table_data, 1 /* ACPI_LINT1 */, 1);
        build_append_int_noprefix(table_data, 0, 3); /* Reserved */
    } else {
        /* Rev 1.0b, 5.2.8.3.3 Local APIC NMI */
        build_append_int_noprefix(table_data, 4, 1);  /* Type */
        build_append_int_noprefix(table_data, 6, 1);  /* Length */
        /* ACPI Processor ID */
        build_append_int_noprefix(table_data, 0xFF /* all processors */, 1);
        build_append_int_noprefix(table_data, 0, 2);  /* Flags */
        /* Local APIC INTI# */
        build_append_int_noprefix(table_data, 1 /* ACPI_LINT1 */, 1);
    }

    acpi_table_end(linker, &table);
}

