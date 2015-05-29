/* Support for generating ACPI tables and passing them to Guests
 *
 * ARM virt ACPI generation
 *
 * Copyright (C) 2008-2010  Kevin O'Connor <kevin@koconnor.net>
 * Copyright (C) 2006 Fabrice Bellard
 * Copyright (C) 2013 Red Hat Inc
 *
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
 * Copyright (c) 2015 HUAWEI TECHNOLOGIES CO.,LTD.
 *
 * Author: Shannon Zhao <zhaoshenglong@huawei.com>
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

#include "qemu-common.h"
#include "hw/arm/virt-acpi-build.h"
#include "qemu/bitmap.h"
#include "trace.h"
#include "qom/cpu.h"
#include "target-arm/cpu.h"
#include "hw/acpi/acpi-defs.h"
#include "hw/acpi/acpi.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/acpi/bios-linker-loader.h"
#include "hw/loader.h"
#include "hw/hw.h"
#include "hw/acpi/aml-build.h"

#define ARM_SPI_BASE 32

static void acpi_dsdt_add_cpus(Aml *scope, int smp_cpus)
{
    uint16_t i;

    for (i = 0; i < smp_cpus; i++) {
        Aml *dev = aml_device("C%03x", i);
        aml_append(dev, aml_name_decl("_HID", aml_string("ACPI0007")));
        aml_append(dev, aml_name_decl("_UID", aml_int(i)));
        aml_append(scope, dev);
    }
}

static void acpi_dsdt_add_uart(Aml *scope, const MemMapEntry *uart_memmap,
                                           int uart_irq)
{
    Aml *dev = aml_device("COM0");
    aml_append(dev, aml_name_decl("_HID", aml_string("ARMH0011")));
    aml_append(dev, aml_name_decl("_UID", aml_int(0)));

    Aml *crs = aml_resource_template();
    aml_append(crs, aml_memory32_fixed(uart_memmap->base,
                                       uart_memmap->size, AML_READ_WRITE));
    aml_append(crs,
               aml_interrupt(AML_CONSUMER, AML_LEVEL, AML_ACTIVE_HIGH,
                             AML_EXCLUSIVE, uart_irq));
    aml_append(dev, aml_name_decl("_CRS", crs));
    aml_append(scope, dev);
}

static void acpi_dsdt_add_rtc(Aml *scope, const MemMapEntry *rtc_memmap,
                                          int rtc_irq)
{
    Aml *dev = aml_device("RTC0");
    aml_append(dev, aml_name_decl("_HID", aml_string("LNRO0013")));
    aml_append(dev, aml_name_decl("_UID", aml_int(0)));

    Aml *crs = aml_resource_template();
    aml_append(crs, aml_memory32_fixed(rtc_memmap->base,
                                       rtc_memmap->size, AML_READ_WRITE));
    aml_append(crs,
               aml_interrupt(AML_CONSUMER, AML_LEVEL, AML_ACTIVE_HIGH,
                             AML_EXCLUSIVE, rtc_irq));
    aml_append(dev, aml_name_decl("_CRS", crs));
    aml_append(scope, dev);
}

static void acpi_dsdt_add_flash(Aml *scope, const MemMapEntry *flash_memmap)
{
    Aml *dev, *crs;
    hwaddr base = flash_memmap->base;
    hwaddr size = flash_memmap->size;

    dev = aml_device("FLS0");
    aml_append(dev, aml_name_decl("_HID", aml_string("LNRO0015")));
    aml_append(dev, aml_name_decl("_UID", aml_int(0)));

    crs = aml_resource_template();
    aml_append(crs, aml_memory32_fixed(base, size, AML_READ_WRITE));
    aml_append(dev, aml_name_decl("_CRS", crs));
    aml_append(scope, dev);

    dev = aml_device("FLS1");
    aml_append(dev, aml_name_decl("_HID", aml_string("LNRO0015")));
    aml_append(dev, aml_name_decl("_UID", aml_int(1)));
    crs = aml_resource_template();
    aml_append(crs, aml_memory32_fixed(base + size, size, AML_READ_WRITE));
    aml_append(dev, aml_name_decl("_CRS", crs));
    aml_append(scope, dev);
}

static void acpi_dsdt_add_virtio(Aml *scope,
                                 const MemMapEntry *virtio_mmio_memmap,
                                 int mmio_irq, int num)
{
    hwaddr base = virtio_mmio_memmap->base;
    hwaddr size = virtio_mmio_memmap->size;
    int irq = mmio_irq;
    int i;

    for (i = 0; i < num; i++) {
        Aml *dev = aml_device("VR%02u", i);
        aml_append(dev, aml_name_decl("_HID", aml_string("LNRO0005")));
        aml_append(dev, aml_name_decl("_UID", aml_int(i)));

        Aml *crs = aml_resource_template();
        aml_append(crs, aml_memory32_fixed(base, size, AML_READ_WRITE));
        aml_append(crs,
                   aml_interrupt(AML_CONSUMER, AML_LEVEL, AML_ACTIVE_HIGH,
                                 AML_EXCLUSIVE, irq + i));
        aml_append(dev, aml_name_decl("_CRS", crs));
        aml_append(scope, dev);
        base += size;
    }
}

/* DSDT */
static void
build_dsdt(GArray *table_data, GArray *linker, VirtGuestInfo *guest_info)
{
    Aml *scope, *dsdt;
    const MemMapEntry *memmap = guest_info->memmap;
    const int *irqmap = guest_info->irqmap;

    dsdt = init_aml_allocator();
    /* Reserve space for header */
    acpi_data_push(dsdt->buf, sizeof(AcpiTableHeader));

    scope = aml_scope("\\_SB");
    acpi_dsdt_add_cpus(scope, guest_info->smp_cpus);
    acpi_dsdt_add_uart(scope, &memmap[VIRT_UART],
                       (irqmap[VIRT_UART] + ARM_SPI_BASE));
    acpi_dsdt_add_rtc(scope, &memmap[VIRT_RTC],
                      (irqmap[VIRT_RTC] + ARM_SPI_BASE));
    acpi_dsdt_add_flash(scope, &memmap[VIRT_FLASH]);
    acpi_dsdt_add_virtio(scope, &memmap[VIRT_MMIO],
                    (irqmap[VIRT_MMIO] + ARM_SPI_BASE), NUM_VIRTIO_TRANSPORTS);
    aml_append(dsdt, scope);

    /* copy AML table into ACPI tables blob and patch header there */
    g_array_append_vals(table_data, dsdt->buf->data, dsdt->buf->len);
    build_header(linker, table_data,
        (void *)(table_data->data + table_data->len - dsdt->buf->len),
        "DSDT", dsdt->buf->len, 5);
    free_aml_allocator();
}

typedef
struct AcpiBuildState {
    /* Copy of table in RAM (for patching). */
    MemoryRegion *table_mr;
    MemoryRegion *rsdp_mr;
    MemoryRegion *linker_mr;
    /* Is table patched? */
    bool patched;
    VirtGuestInfo *guest_info;
} AcpiBuildState;

static
void virt_acpi_build(VirtGuestInfo *guest_info, AcpiBuildTables *tables)
{
    GArray *table_offsets;
    GArray *tables_blob = tables->table_data;

    table_offsets = g_array_new(false, true /* clear */,
                                        sizeof(uint32_t));

    bios_linker_loader_alloc(tables->linker, ACPI_BUILD_TABLE_FILE,
                             64, false /* high memory */);

    /*
     * The ACPI v5.1 tables for Hardware-reduced ACPI platform are:
     * RSDP
     * RSDT
     * FADT
     * GTDT
     * MADT
     * DSDT
     */

    /* DSDT is pointed to by FADT */
    build_dsdt(tables_blob, tables->linker, guest_info);

    /* Cleanup memory that's no longer used. */
    g_array_free(table_offsets, true);
}

static void acpi_ram_update(MemoryRegion *mr, GArray *data)
{
    uint32_t size = acpi_data_len(data);

    /* Make sure RAM size is correct - in case it got changed
     * e.g. by migration */
    memory_region_ram_resize(mr, size, &error_abort);

    memcpy(memory_region_get_ram_ptr(mr), data->data, size);
    memory_region_set_dirty(mr, 0, size);
}

static void virt_acpi_build_update(void *build_opaque, uint32_t offset)
{
    AcpiBuildState *build_state = build_opaque;
    AcpiBuildTables tables;

    /* No state to update or already patched? Nothing to do. */
    if (!build_state || build_state->patched) {
        return;
    }
    build_state->patched = true;

    acpi_build_tables_init(&tables);

    virt_acpi_build(build_state->guest_info, &tables);

    acpi_ram_update(build_state->table_mr, tables.table_data);
    acpi_ram_update(build_state->rsdp_mr, tables.rsdp);
    acpi_ram_update(build_state->linker_mr, tables.linker);


    acpi_build_tables_cleanup(&tables, true);
}

static void virt_acpi_build_reset(void *build_opaque)
{
    AcpiBuildState *build_state = build_opaque;
    build_state->patched = false;
}

static MemoryRegion *acpi_add_rom_blob(AcpiBuildState *build_state,
                                       GArray *blob, const char *name,
                                       uint64_t max_size)
{
    return rom_add_blob(name, blob->data, acpi_data_len(blob), max_size, -1,
                        name, virt_acpi_build_update, build_state);
}

static const VMStateDescription vmstate_virt_acpi_build = {
    .name = "virt_acpi_build",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(patched, AcpiBuildState),
        VMSTATE_END_OF_LIST()
    },
};

void virt_acpi_setup(VirtGuestInfo *guest_info)
{
    AcpiBuildTables tables;
    AcpiBuildState *build_state;

    if (!guest_info->fw_cfg) {
        trace_virt_acpi_setup();
        return;
    }

    if (!acpi_enabled) {
        trace_virt_acpi_setup();
        return;
    }

    build_state = g_malloc0(sizeof *build_state);
    build_state->guest_info = guest_info;

    acpi_build_tables_init(&tables);
    virt_acpi_build(build_state->guest_info, &tables);

    /* Now expose it all to Guest */
    build_state->table_mr = acpi_add_rom_blob(build_state, tables.table_data,
                                               ACPI_BUILD_TABLE_FILE,
                                               ACPI_BUILD_TABLE_MAX_SIZE);
    assert(build_state->table_mr != NULL);

    build_state->linker_mr =
        acpi_add_rom_blob(build_state, tables.linker, "etc/table-loader", 0);

    fw_cfg_add_file(guest_info->fw_cfg, ACPI_BUILD_TPMLOG_FILE,
                    tables.tcpalog->data, acpi_data_len(tables.tcpalog));

    build_state->rsdp_mr = acpi_add_rom_blob(build_state, tables.rsdp,
                                              ACPI_BUILD_RSDP_FILE, 0);

    qemu_register_reset(virt_acpi_build_reset, build_state);
    virt_acpi_build_reset(build_state);
    vmstate_register(NULL, 0, &vmstate_virt_acpi_build, build_state);

    /* Cleanup tables but don't free the memory: we track it
     * in build_state.
     */
    acpi_build_tables_cleanup(&tables, false);
}
