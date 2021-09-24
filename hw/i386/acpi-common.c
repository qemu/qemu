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
    uint32_t flags = apic_ids->cpus[uid].cpu != NULL || force_enabled ? 1 : 0;

    /* ACPI spec says that LAPIC entry for non present
     * CPU may be omitted from MADT or it must be marked
     * as disabled. However omitting non present CPU from
     * MADT breaks hotplug on linux. So possible CPUs
     * should be put in MADT but kept disabled.
     */
    if (apic_id < 255) {
        AcpiMadtProcessorApic *apic = acpi_data_push(entry, sizeof *apic);

        apic->type = ACPI_APIC_PROCESSOR;
        apic->length = sizeof(*apic);
        apic->processor_id = uid;
        apic->local_apic_id = apic_id;
        apic->flags = cpu_to_le32(flags);
    } else {
        AcpiMadtProcessorX2Apic *apic = acpi_data_push(entry, sizeof *apic);

        apic->type = ACPI_APIC_LOCAL_X2APIC;
        apic->length = sizeof(*apic);
        apic->uid = cpu_to_le32(uid);
        apic->x2apic_id = cpu_to_le32(apic_id);
        apic->flags = cpu_to_le32(flags);
    }
}

/*
 * ACPI spec, Revision 1.0b
 * 5.2.8 Multiple APIC Description Table
 */
void acpi_build_madt(GArray *table_data, BIOSLinker *linker,
                     X86MachineState *x86ms, AcpiDeviceIf *adev,
                     const char *oem_id, const char *oem_table_id)
{
    MachineClass *mc = MACHINE_GET_CLASS(x86ms);
    const CPUArchIdList *apic_ids = mc->possible_cpu_arch_ids(MACHINE(x86ms));
    AcpiDeviceIfClass *adevc = ACPI_DEVICE_IF_GET_CLASS(adev);
    bool x2apic_mode = false;

    AcpiMadtIoApic *io_apic;
    AcpiMadtIntsrcovr *intsrcovr;
    int i;
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

    io_apic = acpi_data_push(table_data, sizeof *io_apic);
    io_apic->type = ACPI_APIC_IO;
    io_apic->length = sizeof(*io_apic);
    io_apic->io_apic_id = ACPI_BUILD_IOAPIC_ID;
    io_apic->address = cpu_to_le32(IO_APIC_DEFAULT_ADDRESS);
    io_apic->interrupt = cpu_to_le32(0);

    if (x86ms->ioapic2) {
        AcpiMadtIoApic *io_apic2;
        io_apic2 = acpi_data_push(table_data, sizeof *io_apic);
        io_apic2->type = ACPI_APIC_IO;
        io_apic2->length = sizeof(*io_apic);
        io_apic2->io_apic_id = ACPI_BUILD_IOAPIC_ID + 1;
        io_apic2->address = cpu_to_le32(IO_APIC_SECONDARY_ADDRESS);
        io_apic2->interrupt = cpu_to_le32(IO_APIC_SECONDARY_IRQBASE);
    }

    if (x86ms->apic_xrupt_override) {
        intsrcovr = acpi_data_push(table_data, sizeof *intsrcovr);
        intsrcovr->type   = ACPI_APIC_XRUPT_OVERRIDE;
        intsrcovr->length = sizeof(*intsrcovr);
        intsrcovr->source = 0;
        intsrcovr->gsi    = cpu_to_le32(2);
        intsrcovr->flags  = cpu_to_le16(0); /* conforms to bus specifications */
    }

    for (i = 1; i < 16; i++) {
        if (!(x86ms->pci_irq_mask & (1 << i))) {
            /* No need for a INT source override structure. */
            continue;
        }
        intsrcovr = acpi_data_push(table_data, sizeof *intsrcovr);
        intsrcovr->type   = ACPI_APIC_XRUPT_OVERRIDE;
        intsrcovr->length = sizeof(*intsrcovr);
        intsrcovr->source = i;
        intsrcovr->gsi    = cpu_to_le32(i);
        intsrcovr->flags  = cpu_to_le16(0xd); /* active high, level triggered */
    }

    if (x2apic_mode) {
        AcpiMadtLocalX2ApicNmi *local_nmi;

        local_nmi = acpi_data_push(table_data, sizeof *local_nmi);
        local_nmi->type   = ACPI_APIC_LOCAL_X2APIC_NMI;
        local_nmi->length = sizeof(*local_nmi);
        local_nmi->uid    = 0xFFFFFFFF; /* all processors */
        local_nmi->flags  = cpu_to_le16(0);
        local_nmi->lint   = 1; /* ACPI_LINT1 */
    } else {
        AcpiMadtLocalNmi *local_nmi;

        local_nmi = acpi_data_push(table_data, sizeof *local_nmi);
        local_nmi->type         = ACPI_APIC_LOCAL_NMI;
        local_nmi->length       = sizeof(*local_nmi);
        local_nmi->processor_id = 0xff; /* all processors */
        local_nmi->flags        = cpu_to_le16(0);
        local_nmi->lint         = 1; /* ACPI_LINT1 */
    }

    acpi_table_end(linker, &table);
}

