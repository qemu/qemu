/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Support for generating ACPI tables and passing them to Guests
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/bitmap.h"
#include "hw/pci/pci.h"
#include "hw/core/cpu.h"
#include "target/loongarch/cpu.h"
#include "hw/acpi/acpi-defs.h"
#include "hw/acpi/acpi.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/acpi/bios-linker-loader.h"
#include "migration/vmstate.h"
#include "hw/mem/memory-device.h"
#include "sysemu/reset.h"

/* Supported chipsets: */
#include "hw/pci-host/ls7a.h"
#include "hw/loongarch/virt.h"
#include "hw/acpi/aml-build.h"

#include "hw/acpi/utils.h"
#include "hw/acpi/pci.h"

#include "qom/qom-qobject.h"

#include "hw/acpi/generic_event_device.h"

#define ACPI_BUILD_ALIGN_SIZE             0x1000
#define ACPI_BUILD_TABLE_SIZE             0x20000

#ifdef DEBUG_ACPI_BUILD
#define ACPI_BUILD_DPRINTF(fmt, ...)        \
    do {printf("ACPI_BUILD: " fmt, ## __VA_ARGS__); } while (0)
#else
#define ACPI_BUILD_DPRINTF(fmt, ...)
#endif

/* build FADT */
static void init_common_fadt_data(AcpiFadtData *data)
{
    AcpiFadtData fadt = {
        /* ACPI 5.0: 4.1 Hardware-Reduced ACPI */
        .rev = 5,
        .flags = ((1 << ACPI_FADT_F_HW_REDUCED_ACPI) |
                  (1 << ACPI_FADT_F_RESET_REG_SUP)),

        /* ACPI 5.0: 4.8.3.7 Sleep Control and Status Registers */
        .sleep_ctl = {
            .space_id = AML_AS_SYSTEM_MEMORY,
            .bit_width = 8,
            .address = VIRT_GED_REG_ADDR + ACPI_GED_REG_SLEEP_CTL,
        },
        .sleep_sts = {
            .space_id = AML_AS_SYSTEM_MEMORY,
            .bit_width = 8,
            .address = VIRT_GED_REG_ADDR + ACPI_GED_REG_SLEEP_STS,
        },

        /* ACPI 5.0: 4.8.3.6 Reset Register */
        .reset_reg = {
            .space_id = AML_AS_SYSTEM_MEMORY,
            .bit_width = 8,
            .address = VIRT_GED_REG_ADDR + ACPI_GED_REG_RESET,
        },
        .reset_val = ACPI_GED_RESET_VALUE,
    };
    *data = fadt;
}

static void acpi_align_size(GArray *blob, unsigned align)
{
    /*
     * Align size to multiple of given size. This reduces the chance
     * we need to change size in the future (breaking cross version migration).
     */
    g_array_set_size(blob, ROUND_UP(acpi_data_len(blob), align));
}

/* build FACS */
static void
build_facs(GArray *table_data)
{
    const char *sig = "FACS";
    const uint8_t reserved[40] = {};

    g_array_append_vals(table_data, sig, 4); /* Signature */
    build_append_int_noprefix(table_data, 64, 4); /* Length */
    build_append_int_noprefix(table_data, 0, 4); /* Hardware Signature */
    build_append_int_noprefix(table_data, 0, 4); /* Firmware Waking Vector */
    build_append_int_noprefix(table_data, 0, 4); /* Global Lock */
    build_append_int_noprefix(table_data, 0, 4); /* Flags */
    g_array_append_vals(table_data, reserved, 40); /* Reserved */
}

/* build MADT */
static void
build_madt(GArray *table_data, BIOSLinker *linker, LoongArchMachineState *lams)
{
    MachineState *ms = MACHINE(lams);
    int i;
    AcpiTable table = { .sig = "APIC", .rev = 1, .oem_id = lams->oem_id,
                        .oem_table_id = lams->oem_table_id };

    acpi_table_begin(&table, table_data);

    /* Local APIC Address */
    build_append_int_noprefix(table_data, 0, 4);
    build_append_int_noprefix(table_data, 1 /* PCAT_COMPAT */, 4); /* Flags */

    for (i = 0; i < ms->smp.cpus; i++) {
        /* Processor Core Interrupt Controller Structure */
        build_append_int_noprefix(table_data, 17, 1);    /* Type */
        build_append_int_noprefix(table_data, 15, 1);    /* Length */
        build_append_int_noprefix(table_data, 1, 1);     /* Version */
        build_append_int_noprefix(table_data, i + 1, 4); /* ACPI Processor ID */
        build_append_int_noprefix(table_data, i, 4);     /* Core ID */
        build_append_int_noprefix(table_data, 1, 4);     /* Flags */
    }

    /* Extend I/O Interrupt Controller Structure */
    build_append_int_noprefix(table_data, 20, 1);        /* Type */
    build_append_int_noprefix(table_data, 13, 1);        /* Length */
    build_append_int_noprefix(table_data, 1, 1);         /* Version */
    build_append_int_noprefix(table_data, 3, 1);         /* Cascade */
    build_append_int_noprefix(table_data, 0, 1);         /* Node */
    build_append_int_noprefix(table_data, 0xffff, 8);    /* Node map */

    /* MSI Interrupt Controller Structure */
    build_append_int_noprefix(table_data, 21, 1);        /* Type */
    build_append_int_noprefix(table_data, 19, 1);        /* Length */
    build_append_int_noprefix(table_data, 1, 1);         /* Version */
    build_append_int_noprefix(table_data, VIRT_PCH_MSI_ADDR_LOW, 8);/* Address */
    build_append_int_noprefix(table_data, 0x40, 4);      /* Start */
    build_append_int_noprefix(table_data, 0xc0, 4);      /* Count */

    /* Bridge I/O Interrupt Controller Structure */
    build_append_int_noprefix(table_data, 22, 1);        /* Type */
    build_append_int_noprefix(table_data, 17, 1);        /* Length */
    build_append_int_noprefix(table_data, 1, 1);         /* Version */
    build_append_int_noprefix(table_data, VIRT_PCH_REG_BASE, 8);/* Address */
    build_append_int_noprefix(table_data, 0x1000, 2);    /* Size */
    build_append_int_noprefix(table_data, 0, 2);         /* Id */
    build_append_int_noprefix(table_data, 0x40, 2);      /* Base */

    acpi_table_end(linker, &table);
}

/* build SRAT */
static void
build_srat(GArray *table_data, BIOSLinker *linker, MachineState *machine)
{
    uint64_t i;
    LoongArchMachineState *lams = LOONGARCH_MACHINE(machine);
    MachineState *ms = MACHINE(lams);
    AcpiTable table = { .sig = "SRAT", .rev = 1, .oem_id = lams->oem_id,
                        .oem_table_id = lams->oem_table_id };

    acpi_table_begin(&table, table_data);
    build_append_int_noprefix(table_data, 1, 4); /* Reserved */
    build_append_int_noprefix(table_data, 0, 8); /* Reserved */

    for (i = 0; i < ms->smp.cpus; ++i) {
        /* Processor Local APIC/SAPIC Affinity Structure */
        build_append_int_noprefix(table_data, 0, 1);  /* Type  */
        build_append_int_noprefix(table_data, 16, 1); /* Length */
        /* Proximity Domain [7:0] */
        build_append_int_noprefix(table_data, 0, 1);
        build_append_int_noprefix(table_data, i, 1); /* APIC ID */
        /* Flags, Table 5-36 */
        build_append_int_noprefix(table_data, 1, 4);
        build_append_int_noprefix(table_data, 0, 1); /* Local SAPIC EID */
        /* Proximity Domain [31:8] */
        build_append_int_noprefix(table_data, 0, 3);
        build_append_int_noprefix(table_data, 0, 4); /* Reserved */
    }

    build_srat_memory(table_data, VIRT_LOWMEM_BASE, VIRT_LOWMEM_SIZE,
                      0, MEM_AFFINITY_ENABLED);

    build_srat_memory(table_data, VIRT_HIGHMEM_BASE, machine->ram_size - VIRT_LOWMEM_SIZE,
                      0, MEM_AFFINITY_ENABLED);

    acpi_table_end(linker, &table);
}

typedef
struct AcpiBuildState {
    /* Copy of table in RAM (for patching). */
    MemoryRegion *table_mr;
    /* Is table patched? */
    uint8_t patched;
    void *rsdp;
    MemoryRegion *rsdp_mr;
    MemoryRegion *linker_mr;
} AcpiBuildState;

static void build_gpex_pci0_int(Aml *table)
{
    Aml *sb_scope = aml_scope("_SB");
    Aml *pci0_scope = aml_scope("PCI0");
    Aml *prt_pkg = aml_varpackage(128);
    int slot, pin;

    for (slot = 0; slot < PCI_SLOT_MAX; slot++) {
        for (pin = 0; pin < PCI_NUM_PINS; pin++) {
            Aml *pkg = aml_package(4);
            aml_append(pkg, aml_int((slot << 16) | 0xFFFF));
            aml_append(pkg, aml_int(pin));
            aml_append(pkg, aml_int(0));
            aml_append(pkg, aml_int(80 + (slot + pin) % 4));
            aml_append(prt_pkg, pkg);
        }
    }
    aml_append(pci0_scope, aml_name_decl("_PRT", prt_pkg));
    aml_append(sb_scope, pci0_scope);
    aml_append(table, sb_scope);
}

static void build_dbg_aml(Aml *table)
{
    Aml *field;
    Aml *method;
    Aml *while_ctx;
    Aml *scope = aml_scope("\\");
    Aml *buf = aml_local(0);
    Aml *len = aml_local(1);
    Aml *idx = aml_local(2);

    aml_append(scope,
       aml_operation_region("DBG", AML_SYSTEM_IO, aml_int(0x0402), 0x01));
    field = aml_field("DBG", AML_BYTE_ACC, AML_NOLOCK, AML_PRESERVE);
    aml_append(field, aml_named_field("DBGB", 8));
    aml_append(scope, field);

    method = aml_method("DBUG", 1, AML_NOTSERIALIZED);

    aml_append(method, aml_to_hexstring(aml_arg(0), buf));
    aml_append(method, aml_to_buffer(buf, buf));
    aml_append(method, aml_subtract(aml_sizeof(buf), aml_int(1), len));
    aml_append(method, aml_store(aml_int(0), idx));

    while_ctx = aml_while(aml_lless(idx, len));
    aml_append(while_ctx,
        aml_store(aml_derefof(aml_index(buf, idx)), aml_name("DBGB")));
    aml_append(while_ctx, aml_increment(idx));
    aml_append(method, while_ctx);
    aml_append(method, aml_store(aml_int(0x0A), aml_name("DBGB")));
    aml_append(scope, method);
    aml_append(table, scope);
}

static Aml *build_osc_method(void)
{
    Aml *if_ctx;
    Aml *if_ctx2;
    Aml *else_ctx;
    Aml *method;
    Aml *a_cwd1 = aml_name("CDW1");
    Aml *a_ctrl = aml_local(0);

    method = aml_method("_OSC", 4, AML_NOTSERIALIZED);
    aml_append(method, aml_create_dword_field(aml_arg(3), aml_int(0), "CDW1"));

    if_ctx = aml_if(aml_equal(
        aml_arg(0), aml_touuid("33DB4D5B-1FF7-401C-9657-7441C03DD766")));
    aml_append(if_ctx, aml_create_dword_field(aml_arg(3), aml_int(4), "CDW2"));
    aml_append(if_ctx, aml_create_dword_field(aml_arg(3), aml_int(8), "CDW3"));
    aml_append(if_ctx, aml_store(aml_name("CDW3"), a_ctrl));

    /*
     * Always allow native PME, AER (no dependencies)
     * Allow SHPC (PCI bridges can have SHPC controller)
     */
    aml_append(if_ctx, aml_and(a_ctrl, aml_int(0x1F), a_ctrl));

    if_ctx2 = aml_if(aml_lnot(aml_equal(aml_arg(1), aml_int(1))));
    /* Unknown revision */
    aml_append(if_ctx2, aml_or(a_cwd1, aml_int(0x08), a_cwd1));
    aml_append(if_ctx, if_ctx2);

    if_ctx2 = aml_if(aml_lnot(aml_equal(aml_name("CDW3"), a_ctrl)));
    /* Capabilities bits were masked */
    aml_append(if_ctx2, aml_or(a_cwd1, aml_int(0x10), a_cwd1));
    aml_append(if_ctx, if_ctx2);

    /* Update DWORD3 in the buffer */
    aml_append(if_ctx, aml_store(a_ctrl, aml_name("CDW3")));
    aml_append(method, if_ctx);

    else_ctx = aml_else();
    /* Unrecognized UUID */
    aml_append(else_ctx, aml_or(a_cwd1, aml_int(4), a_cwd1));
    aml_append(method, else_ctx);

    aml_append(method, aml_return(aml_arg(3)));
    return method;
}

static void build_uart_device_aml(Aml *table)
{
    Aml *dev;
    Aml *crs;
    Aml *pkg0, *pkg1, *pkg2;
    uint32_t uart_irq = VIRT_UART_IRQ;

    Aml *scope = aml_scope("_SB");
    dev = aml_device("COMA");
    aml_append(dev, aml_name_decl("_HID", aml_string("PNP0501")));
    aml_append(dev, aml_name_decl("_UID", aml_int(0)));
    aml_append(dev, aml_name_decl("_CCA", aml_int(1)));
    crs = aml_resource_template();
    aml_append(crs,
        aml_qword_memory(AML_POS_DECODE, AML_MIN_FIXED, AML_MAX_FIXED,
                         AML_NON_CACHEABLE, AML_READ_WRITE,
                         0, 0x1FE001E0, 0x1FE001E7, 0, 0x8));
    aml_append(crs, aml_interrupt(AML_CONSUMER, AML_LEVEL, AML_ACTIVE_HIGH,
                                  AML_SHARED, &uart_irq, 1));
    aml_append(dev, aml_name_decl("_CRS", crs));
    pkg0 = aml_package(0x2);
    aml_append(pkg0, aml_int(0x05F5E100));
    aml_append(pkg0, aml_string("clock-frenquency"));
    pkg1 = aml_package(0x1);
    aml_append(pkg1, pkg0);
    pkg2 = aml_package(0x2);
    aml_append(pkg2, aml_touuid("DAFFD814-6EBA-4D8C-8A91-BC9BBF4AA301"));
    aml_append(pkg2, pkg1);
    aml_append(dev, aml_name_decl("_DSD", pkg2));
    aml_append(scope, dev);
    aml_append(table, scope);
}

/* build DSDT */
static void
build_dsdt(GArray *table_data, BIOSLinker *linker, MachineState *machine)
{
    Aml *dsdt, *sb_scope, *scope, *dev, *crs, *pkg;
    int root_bus_limit = 0x7F;
    LoongArchMachineState *lams = LOONGARCH_MACHINE(machine);
    AcpiTable table = { .sig = "DSDT", .rev = 1, .oem_id = lams->oem_id,
                        .oem_table_id = lams->oem_table_id };

    acpi_table_begin(&table, table_data);

    dsdt = init_aml_allocator();

    build_dbg_aml(dsdt);

    sb_scope = aml_scope("_SB");
    dev = aml_device("PCI0");
    aml_append(dev, aml_name_decl("_HID", aml_eisaid("PNP0A08")));
    aml_append(dev, aml_name_decl("_CID", aml_eisaid("PNP0A03")));
    aml_append(dev, aml_name_decl("_ADR", aml_int(0)));
    aml_append(dev, aml_name_decl("_BBN", aml_int(0)));
    aml_append(dev, aml_name_decl("_UID", aml_int(1)));
    aml_append(dev, build_osc_method());
    aml_append(sb_scope, dev);
    aml_append(dsdt, sb_scope);

    build_gpex_pci0_int(dsdt);
    build_uart_device_aml(dsdt);
    if (lams->acpi_ged) {
        build_ged_aml(dsdt, "\\_SB."GED_DEVICE,
                      HOTPLUG_HANDLER(lams->acpi_ged),
                      VIRT_SCI_IRQ - PCH_PIC_IRQ_OFFSET, AML_SYSTEM_MEMORY,
                      VIRT_GED_EVT_ADDR);
    }

    scope = aml_scope("\\_SB.PCI0");
    /* Build PCI0._CRS */
    crs = aml_resource_template();
    aml_append(crs,
        aml_word_bus_number(AML_MIN_FIXED, AML_MAX_FIXED, AML_POS_DECODE,
                            0x0000, 0x0, root_bus_limit,
                            0x0000, root_bus_limit + 1));
    aml_append(crs,
        aml_dword_io(AML_MIN_FIXED, AML_MAX_FIXED,
                    AML_POS_DECODE, AML_ENTIRE_RANGE,
                    0x0000, 0x0000, 0xFFFF, 0x18000000, 0x10000));
    aml_append(crs,
        aml_dword_memory(AML_POS_DECODE, AML_MIN_FIXED, AML_MAX_FIXED,
                         AML_CACHEABLE, AML_READ_WRITE,
                         0, VIRT_PCI_MEM_BASE,
                         VIRT_PCI_MEM_BASE + VIRT_PCI_MEM_SIZE - 1,
                         0, VIRT_PCI_MEM_BASE));
    aml_append(scope, aml_name_decl("_CRS", crs));
    aml_append(dsdt, scope);

    /* System State Package */
    scope = aml_scope("\\");
    pkg = aml_package(4);
    aml_append(pkg, aml_int(ACPI_GED_SLP_TYP_S5));
    aml_append(pkg, aml_int(0)); /* ignored */
    aml_append(pkg, aml_int(0)); /* reserved */
    aml_append(pkg, aml_int(0)); /* reserved */
    aml_append(scope, aml_name_decl("_S5", pkg));
    aml_append(dsdt, scope);
    /* Copy AML table into ACPI tables blob and patch header there */
    g_array_append_vals(table_data, dsdt->buf->data, dsdt->buf->len);
    acpi_table_end(linker, &table);
    free_aml_allocator();
}

static void acpi_build(AcpiBuildTables *tables, MachineState *machine)
{
    LoongArchMachineState *lams = LOONGARCH_MACHINE(machine);
    GArray *table_offsets;
    AcpiFadtData fadt_data;
    unsigned facs, rsdt, dsdt;
    uint8_t *u;
    GArray *tables_blob = tables->table_data;

    init_common_fadt_data(&fadt_data);

    table_offsets = g_array_new(false, true, sizeof(uint32_t));
    ACPI_BUILD_DPRINTF("init ACPI tables\n");

    bios_linker_loader_alloc(tables->linker,
                             ACPI_BUILD_TABLE_FILE, tables_blob,
                             64, false);

    /*
     * FACS is pointed to by FADT.
     * We place it first since it's the only table that has alignment
     * requirements.
     */
    facs = tables_blob->len;
    build_facs(tables_blob);

    /* DSDT is pointed to by FADT */
    dsdt = tables_blob->len;
    build_dsdt(tables_blob, tables->linker, machine);

    /* ACPI tables pointed to by RSDT */
    acpi_add_table(table_offsets, tables_blob);
    fadt_data.facs_tbl_offset = &facs;
    fadt_data.dsdt_tbl_offset = &dsdt;
    fadt_data.xdsdt_tbl_offset = &dsdt;
    build_fadt(tables_blob, tables->linker, &fadt_data,
               lams->oem_id, lams->oem_table_id);

    acpi_add_table(table_offsets, tables_blob);
    build_madt(tables_blob, tables->linker, lams);

    acpi_add_table(table_offsets, tables_blob);
    build_srat(tables_blob, tables->linker, machine);

    acpi_add_table(table_offsets, tables_blob);
    {
        AcpiMcfgInfo mcfg = {
           .base = cpu_to_le64(VIRT_PCI_CFG_BASE),
           .size = cpu_to_le64(VIRT_PCI_CFG_SIZE),
        };
        build_mcfg(tables_blob, tables->linker, &mcfg, lams->oem_id,
                   lams->oem_table_id);
    }

    /* Add tables supplied by user (if any) */
    for (u = acpi_table_first(); u; u = acpi_table_next(u)) {
        unsigned len = acpi_table_len(u);

        acpi_add_table(table_offsets, tables_blob);
        g_array_append_vals(tables_blob, u, len);
    }

    /* RSDT is pointed to by RSDP */
    rsdt = tables_blob->len;
    build_rsdt(tables_blob, tables->linker, table_offsets,
               lams->oem_id, lams->oem_table_id);

    /* RSDP is in FSEG memory, so allocate it separately */
    {
        AcpiRsdpData rsdp_data = {
            .revision = 0,
            .oem_id = lams->oem_id,
            .xsdt_tbl_offset = NULL,
            .rsdt_tbl_offset = &rsdt,
        };
        build_rsdp(tables->rsdp, tables->linker, &rsdp_data);
    }

    /*
     * The align size is 128, warn if 64k is not enough therefore
     * the align size could be resized.
     */
    if (tables_blob->len > ACPI_BUILD_TABLE_SIZE / 2) {
        warn_report("ACPI table size %u exceeds %d bytes,"
                    " migration may not work",
                    tables_blob->len, ACPI_BUILD_TABLE_SIZE / 2);
        error_printf("Try removing CPUs, NUMA nodes, memory slots"
                     " or PCI bridges.");
    }

    acpi_align_size(tables->linker->cmd_blob, ACPI_BUILD_ALIGN_SIZE);

    /* Cleanup memory that's no longer used. */
    g_array_free(table_offsets, true);
}

static void acpi_ram_update(MemoryRegion *mr, GArray *data)
{
    uint32_t size = acpi_data_len(data);

    /*
     * Make sure RAM size is correct - in case it got changed
     * e.g. by migration
     */
    memory_region_ram_resize(mr, size, &error_abort);

    memcpy(memory_region_get_ram_ptr(mr), data->data, size);
    memory_region_set_dirty(mr, 0, size);
}

static void acpi_build_update(void *build_opaque)
{
    AcpiBuildState *build_state = build_opaque;
    AcpiBuildTables tables;

    /* No state to update or already patched? Nothing to do. */
    if (!build_state || build_state->patched) {
        return;
    }
    build_state->patched = 1;

    acpi_build_tables_init(&tables);

    acpi_build(&tables, MACHINE(qdev_get_machine()));

    acpi_ram_update(build_state->table_mr, tables.table_data);
    acpi_ram_update(build_state->rsdp_mr, tables.rsdp);
    acpi_ram_update(build_state->linker_mr, tables.linker->cmd_blob);

    acpi_build_tables_cleanup(&tables, true);
}

static void acpi_build_reset(void *build_opaque)
{
    AcpiBuildState *build_state = build_opaque;
    build_state->patched = 0;
}

static const VMStateDescription vmstate_acpi_build = {
    .name = "acpi_build",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(patched, AcpiBuildState),
        VMSTATE_END_OF_LIST()
    },
};

void loongarch_acpi_setup(LoongArchMachineState *lams)
{
    AcpiBuildTables tables;
    AcpiBuildState *build_state;

    if (!lams->fw_cfg) {
        ACPI_BUILD_DPRINTF("No fw cfg. Bailing out.\n");
        return;
    }

    if (!loongarch_is_acpi_enabled(lams)) {
        ACPI_BUILD_DPRINTF("ACPI disabled. Bailing out.\n");
        return;
    }

    build_state = g_malloc0(sizeof *build_state);

    acpi_build_tables_init(&tables);
    acpi_build(&tables, MACHINE(lams));

    /* Now expose it all to Guest */
    build_state->table_mr = acpi_add_rom_blob(acpi_build_update,
                                              build_state, tables.table_data,
                                              ACPI_BUILD_TABLE_FILE);
    assert(build_state->table_mr != NULL);

    build_state->linker_mr =
        acpi_add_rom_blob(acpi_build_update, build_state,
                          tables.linker->cmd_blob, ACPI_BUILD_LOADER_FILE);

    build_state->rsdp_mr = acpi_add_rom_blob(acpi_build_update,
                                             build_state, tables.rsdp,
                                             ACPI_BUILD_RSDP_FILE);

    qemu_register_reset(acpi_build_reset, build_state);
    acpi_build_reset(build_state);
    vmstate_register(NULL, 0, &vmstate_acpi_build, build_state);

    /*
     * Cleanup tables but don't free the memory: we track it
     * in build_state.
     */
    acpi_build_tables_cleanup(&tables, false);
}
