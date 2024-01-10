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
#include "qemu/cutils.h"
#include "qapi/error.h"

#include "exec/memory.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/acpi_aml_interface.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/bios-linker-loader.h"
#include "hw/acpi/generic_event_device.h"
#include "hw/acpi/utils.h"
#include "hw/acpi/erst.h"
#include "hw/i386/fw_cfg.h"
#include "hw/i386/microvm.h"
#include "hw/pci/pci.h"
#include "hw/pci/pcie_host.h"
#include "hw/usb/xhci.h"
#include "hw/virtio/virtio-acpi.h"
#include "hw/virtio/virtio-mmio.h"
#include "hw/input/i8042.h"

#include "acpi-common.h"
#include "acpi-microvm.h"

#include CONFIG_DEVICES

static void acpi_dsdt_add_virtio(Aml *scope,
                                 MicrovmMachineState *mms)
{
    gchar *separator;
    long int index;
    BusState *bus;
    BusChild *kid;

    bus = sysbus_get_default();
    QTAILQ_FOREACH(kid, &bus->children, sibling) {
        Object *obj = object_dynamic_cast(OBJECT(kid->child),
                                          TYPE_VIRTIO_MMIO);

        if (obj) {
            VirtIOMMIOProxy *mmio = VIRTIO_MMIO(obj);
            VirtioBusState *mmio_virtio_bus = &mmio->bus;
            BusState *mmio_bus = &mmio_virtio_bus->parent_obj;

            if (QTAILQ_EMPTY(&mmio_bus->children)) {
                continue;
            }
            separator = g_strrstr(mmio_bus->name, ".");
            if (!separator) {
                continue;
            }
            if (qemu_strtol(separator + 1, NULL, 10, &index) != 0) {
                continue;
            }

            uint32_t irq = mms->virtio_irq_base + index;
            hwaddr base = VIRTIO_MMIO_BASE + index * 512;
            hwaddr size = 512;
            virtio_acpi_dsdt_add(scope, base, size, irq, index, 1);
        }
    }
}

static void acpi_dsdt_add_xhci(Aml *scope, MicrovmMachineState *mms)
{
    if (machine_usb(MACHINE(mms))) {
        xhci_sysbus_build_aml(scope, MICROVM_XHCI_BASE, MICROVM_XHCI_IRQ);
    }
}

static void acpi_dsdt_add_pci(Aml *scope, MicrovmMachineState *mms)
{
    if (mms->pcie != ON_OFF_AUTO_ON) {
        return;
    }

    acpi_dsdt_add_gpex(scope, &mms->gpex);
}

static void
build_dsdt_microvm(GArray *table_data, BIOSLinker *linker,
                   MicrovmMachineState *mms)
{
    X86MachineState *x86ms = X86_MACHINE(mms);
    Aml *dsdt, *sb_scope, *scope, *pkg;
    bool ambiguous;
    Object *isabus;
    AcpiTable table = { .sig = "DSDT", .rev = 2, .oem_id = x86ms->oem_id,
                        .oem_table_id = x86ms->oem_table_id };

    isabus = object_resolve_path_type("", TYPE_ISA_BUS, &ambiguous);
    assert(isabus);
    assert(!ambiguous);

    acpi_table_begin(&table, table_data);
    dsdt = init_aml_allocator();

    sb_scope = aml_scope("_SB");
    fw_cfg_add_acpi_dsdt(sb_scope, x86ms->fw_cfg);
    qbus_build_aml(BUS(isabus), sb_scope);
    build_ged_aml(sb_scope, GED_DEVICE, x86ms->acpi_dev,
                  GED_MMIO_IRQ, AML_SYSTEM_MEMORY, GED_MMIO_BASE);
    acpi_dsdt_add_power_button(sb_scope);
    acpi_dsdt_add_virtio(sb_scope, mms);
    acpi_dsdt_add_xhci(sb_scope, mms);
    acpi_dsdt_add_pci(sb_scope, mms);
    aml_append(dsdt, sb_scope);

    /* ACPI 5.0: Table 7-209 System State Package */
    scope = aml_scope("\\");
    pkg = aml_package(4);
    aml_append(pkg, aml_int(ACPI_GED_SLP_TYP_S5));
    aml_append(pkg, aml_int(0)); /* ignored */
    aml_append(pkg, aml_int(0)); /* reserved */
    aml_append(pkg, aml_int(0)); /* reserved */
    aml_append(scope, aml_name_decl("_S5", pkg));
    aml_append(dsdt, scope);

    /* copy AML bytecode into ACPI tables blob */
    g_array_append_vals(table_data, dsdt->buf->data, dsdt->buf->len);

    acpi_table_end(linker, &table);
    free_aml_allocator();
}

static void acpi_build_microvm(AcpiBuildTables *tables,
                               MicrovmMachineState *mms)
{
    MachineState *machine = MACHINE(mms);
    X86MachineState *x86ms = X86_MACHINE(mms);
    GArray *table_offsets;
    GArray *tables_blob = tables->table_data;
    unsigned dsdt, xsdt;
    AcpiFadtData pmfadt = {
        /* ACPI 5.0: 4.1 Hardware-Reduced ACPI */
        .rev = 5,
        .flags = ((1 << ACPI_FADT_F_HW_REDUCED_ACPI) |
                  (1 << ACPI_FADT_F_RESET_REG_SUP)),

        /* ACPI 5.0: 4.8.3.7 Sleep Control and Status Registers */
        .sleep_ctl = {
            .space_id = AML_AS_SYSTEM_MEMORY,
            .bit_width = 8,
            .address = GED_MMIO_BASE_REGS + ACPI_GED_REG_SLEEP_CTL,
        },
        .sleep_sts = {
            .space_id = AML_AS_SYSTEM_MEMORY,
            .bit_width = 8,
            .address = GED_MMIO_BASE_REGS + ACPI_GED_REG_SLEEP_STS,
        },

        /* ACPI 5.0: 4.8.3.6 Reset Register */
        .reset_reg = {
            .space_id = AML_AS_SYSTEM_MEMORY,
            .bit_width = 8,
            .address = GED_MMIO_BASE_REGS + ACPI_GED_REG_RESET,
        },
        .reset_val = ACPI_GED_RESET_VALUE,
        /*
         * ACPI v2, Table 5-10 - Fixed ACPI Description Table Boot Architecture
         * Flags, bit offset 1 - 8042.
         */
        .iapc_boot_arch = iapc_boot_arch_8042(),
    };

    table_offsets = g_array_new(false, true /* clear */,
                                        sizeof(uint32_t));
    bios_linker_loader_alloc(tables->linker,
                             ACPI_BUILD_TABLE_FILE, tables_blob,
                             64 /* Ensure FACS is aligned */,
                             false /* high memory */);

    dsdt = tables_blob->len;
    build_dsdt_microvm(tables_blob, tables->linker, mms);

    pmfadt.dsdt_tbl_offset = &dsdt;
    pmfadt.xdsdt_tbl_offset = &dsdt;
    acpi_add_table(table_offsets, tables_blob);
    build_fadt(tables_blob, tables->linker, &pmfadt, x86ms->oem_id,
               x86ms->oem_table_id);

    acpi_add_table(table_offsets, tables_blob);
    acpi_build_madt(tables_blob, tables->linker, X86_MACHINE(machine),
                    x86ms->oem_id, x86ms->oem_table_id);

#ifdef CONFIG_ACPI_ERST
    {
        Object *erst_dev;
        erst_dev = find_erst_dev();
        if (erst_dev) {
            acpi_add_table(table_offsets, tables_blob);
            build_erst(tables_blob, tables->linker, erst_dev,
                       x86ms->oem_id, x86ms->oem_table_id);
        }
    }
#endif

    xsdt = tables_blob->len;
    build_xsdt(tables_blob, tables->linker, table_offsets, x86ms->oem_id,
               x86ms->oem_table_id);

    /* RSDP is in FSEG memory, so allocate it separately */
    {
        AcpiRsdpData rsdp_data = {
            /* ACPI 2.0: 5.2.4.3 RSDP Structure */
            .revision = 2, /* xsdt needs v2 */
            .oem_id = x86ms->oem_id,
            .xsdt_tbl_offset = &xsdt,
            .rsdt_tbl_offset = NULL,
        };
        build_rsdp(tables->rsdp, tables->linker, &rsdp_data);
    }

    /* Cleanup memory that's no longer used. */
    g_array_free(table_offsets, true);
}

static void acpi_build_no_update(void *build_opaque)
{
    /* nothing, microvm tables don't change at runtime */
}

void acpi_setup_microvm(MicrovmMachineState *mms)
{
    X86MachineState *x86ms = X86_MACHINE(mms);
    AcpiBuildTables tables;

    assert(x86ms->fw_cfg);

    if (!x86_machine_is_acpi_enabled(x86ms)) {
        return;
    }

    acpi_build_tables_init(&tables);
    acpi_build_microvm(&tables, mms);

    /* Now expose it all to Guest */
    acpi_add_rom_blob(acpi_build_no_update, NULL, tables.table_data,
                      ACPI_BUILD_TABLE_FILE);
    acpi_add_rom_blob(acpi_build_no_update, NULL, tables.linker->cmd_blob,
                      ACPI_BUILD_LOADER_FILE);
    acpi_add_rom_blob(acpi_build_no_update, NULL, tables.rsdp,
                      ACPI_BUILD_RSDP_FILE);

    acpi_build_tables_cleanup(&tables, false);
}
