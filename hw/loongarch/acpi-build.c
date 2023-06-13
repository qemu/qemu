/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Support for generating ACPI tables and passing them to Guests
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
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

#include "hw/acpi/utils.h"
#include "hw/acpi/pci.h"

#include "qom/qom-qobject.h"

#include "hw/acpi/generic_event_device.h"
#include "hw/pci-host/gpex.h"
#include "sysemu/tpm.h"
#include "hw/platform-bus.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/hmat.h"

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
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    const CPUArchIdList *arch_ids = mc->possible_cpu_arch_ids(ms);
    int i, arch_id;
    AcpiTable table = { .sig = "APIC", .rev = 1, .oem_id = lams->oem_id,
                        .oem_table_id = lams->oem_table_id };

    acpi_table_begin(&table, table_data);

    /* Local APIC Address */
    build_append_int_noprefix(table_data, 0, 4);
    build_append_int_noprefix(table_data, 1 /* PCAT_COMPAT */, 4); /* Flags */

    for (i = 0; i < arch_ids->len; i++) {
        /* Processor Core Interrupt Controller Structure */
        arch_id = arch_ids->cpus[i].arch_id;

        build_append_int_noprefix(table_data, 17, 1);    /* Type */
        build_append_int_noprefix(table_data, 15, 1);    /* Length */
        build_append_int_noprefix(table_data, 1, 1);     /* Version */
        build_append_int_noprefix(table_data, i + 1, 4); /* ACPI Processor ID */
        build_append_int_noprefix(table_data, arch_id, 4); /* Core ID */
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
    int i, arch_id, node_id;
    uint64_t mem_len, mem_base;
    int nb_numa_nodes = machine->numa_state->num_nodes;
    LoongArchMachineState *lams = LOONGARCH_MACHINE(machine);
    MachineClass *mc = MACHINE_GET_CLASS(lams);
    const CPUArchIdList *arch_ids = mc->possible_cpu_arch_ids(machine);
    AcpiTable table = { .sig = "SRAT", .rev = 1, .oem_id = lams->oem_id,
                        .oem_table_id = lams->oem_table_id };

    acpi_table_begin(&table, table_data);
    build_append_int_noprefix(table_data, 1, 4); /* Reserved */
    build_append_int_noprefix(table_data, 0, 8); /* Reserved */

    for (i = 0; i < arch_ids->len; ++i) {
        arch_id = arch_ids->cpus[i].arch_id;
        node_id = arch_ids->cpus[i].props.node_id;

        /* Processor Local APIC/SAPIC Affinity Structure */
        build_append_int_noprefix(table_data, 0, 1);  /* Type  */
        build_append_int_noprefix(table_data, 16, 1); /* Length */
        /* Proximity Domain [7:0] */
        build_append_int_noprefix(table_data, node_id, 1);
        build_append_int_noprefix(table_data, arch_id, 1); /* APIC ID */
        /* Flags, Table 5-36 */
        build_append_int_noprefix(table_data, 1, 4);
        build_append_int_noprefix(table_data, 0, 1); /* Local SAPIC EID */
        /* Proximity Domain [31:8] */
        build_append_int_noprefix(table_data, 0, 3);
        build_append_int_noprefix(table_data, 0, 4); /* Reserved */
    }

    /* Node0 */
    build_srat_memory(table_data, VIRT_LOWMEM_BASE, VIRT_LOWMEM_SIZE,
                      0, MEM_AFFINITY_ENABLED);
    mem_base = VIRT_HIGHMEM_BASE;
    if (!nb_numa_nodes) {
        mem_len = machine->ram_size - VIRT_LOWMEM_SIZE;
    } else {
        mem_len = machine->numa_state->nodes[0].node_mem - VIRT_LOWMEM_SIZE;
    }
    if (mem_len)
        build_srat_memory(table_data, mem_base, mem_len, 0, MEM_AFFINITY_ENABLED);

    /* Node1 - Nodemax */
    if (nb_numa_nodes) {
        mem_base += mem_len;
        for (i = 1; i < nb_numa_nodes; ++i) {
            if (machine->numa_state->nodes[i].node_mem > 0) {
                build_srat_memory(table_data, mem_base,
                                  machine->numa_state->nodes[i].node_mem, i,
                                  MEM_AFFINITY_ENABLED);
                mem_base += machine->numa_state->nodes[i].node_mem;
            }
        }
    }

    if (machine->device_memory) {
        build_srat_memory(table_data, machine->device_memory->base,
                          memory_region_size(&machine->device_memory->mr),
                          nb_numa_nodes - 1,
                          MEM_AFFINITY_HOTPLUGGABLE | MEM_AFFINITY_ENABLED);
    }

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
                         0, VIRT_UART_BASE, VIRT_UART_BASE + VIRT_UART_SIZE - 1,
                         0, VIRT_UART_SIZE));
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

static void
build_la_ged_aml(Aml *dsdt, MachineState *machine)
{
    uint32_t event;
    LoongArchMachineState *lams = LOONGARCH_MACHINE(machine);

    build_ged_aml(dsdt, "\\_SB."GED_DEVICE,
                  HOTPLUG_HANDLER(lams->acpi_ged),
                  VIRT_SCI_IRQ, AML_SYSTEM_MEMORY,
                  VIRT_GED_EVT_ADDR);
    event = object_property_get_uint(OBJECT(lams->acpi_ged),
                                     "ged-event", &error_abort);
    if (event & ACPI_GED_MEM_HOTPLUG_EVT) {
        build_memory_hotplug_aml(dsdt, machine->ram_slots, "\\_SB", NULL,
                                 AML_SYSTEM_MEMORY,
                                 VIRT_GED_MEM_ADDR);
    }
    acpi_dsdt_add_power_button(dsdt);
}

static void build_pci_device_aml(Aml *scope, LoongArchMachineState *lams)
{
    struct GPEXConfig cfg = {
        .mmio64.base = VIRT_PCI_MEM_BASE,
        .mmio64.size = VIRT_PCI_MEM_SIZE,
        .pio.base    = VIRT_PCI_IO_BASE,
        .pio.size    = VIRT_PCI_IO_SIZE,
        .ecam.base   = VIRT_PCI_CFG_BASE,
        .ecam.size   = VIRT_PCI_CFG_SIZE,
        .irq         = VIRT_GSI_BASE + VIRT_DEVICE_IRQS,
        .bus         = lams->pci_bus,
    };

    acpi_dsdt_add_gpex(scope, &cfg);
}

static void build_flash_aml(Aml *scope, LoongArchMachineState *lams)
{
    Aml *dev, *crs;

    hwaddr flash_base = VIRT_FLASH_BASE;
    hwaddr flash_size = VIRT_FLASH_SIZE;

    dev = aml_device("FLS0");
    aml_append(dev, aml_name_decl("_HID", aml_string("LNRO0015")));
    aml_append(dev, aml_name_decl("_UID", aml_int(0)));

    crs = aml_resource_template();
    aml_append(crs, aml_memory32_fixed(flash_base, flash_size, AML_READ_WRITE));
    aml_append(dev, aml_name_decl("_CRS", crs));
    aml_append(scope, dev);
}

#ifdef CONFIG_TPM
static void acpi_dsdt_add_tpm(Aml *scope, LoongArchMachineState *vms)
{
    PlatformBusDevice *pbus = PLATFORM_BUS_DEVICE(vms->platform_bus_dev);
    hwaddr pbus_base = VIRT_PLATFORM_BUS_BASEADDRESS;
    SysBusDevice *sbdev = SYS_BUS_DEVICE(tpm_find());
    MemoryRegion *sbdev_mr;
    hwaddr tpm_base;

    if (!sbdev) {
        return;
    }

    tpm_base = platform_bus_get_mmio_addr(pbus, sbdev, 0);
    assert(tpm_base != -1);

    tpm_base += pbus_base;

    sbdev_mr = sysbus_mmio_get_region(sbdev, 0);

    Aml *dev = aml_device("TPM0");
    aml_append(dev, aml_name_decl("_HID", aml_string("MSFT0101")));
    aml_append(dev, aml_name_decl("_STR", aml_string("TPM 2.0 Device")));
    aml_append(dev, aml_name_decl("_UID", aml_int(0)));

    Aml *crs = aml_resource_template();
    aml_append(crs,
               aml_memory32_fixed(tpm_base,
                                  (uint32_t)memory_region_size(sbdev_mr),
                                  AML_READ_WRITE));
    aml_append(dev, aml_name_decl("_CRS", crs));
    aml_append(scope, dev);
}
#endif

/* build DSDT */
static void
build_dsdt(GArray *table_data, BIOSLinker *linker, MachineState *machine)
{
    Aml *dsdt, *scope, *pkg;
    LoongArchMachineState *lams = LOONGARCH_MACHINE(machine);
    AcpiTable table = { .sig = "DSDT", .rev = 1, .oem_id = lams->oem_id,
                        .oem_table_id = lams->oem_table_id };

    acpi_table_begin(&table, table_data);
    dsdt = init_aml_allocator();
    build_uart_device_aml(dsdt);
    build_pci_device_aml(dsdt, lams);
    build_la_ged_aml(dsdt, machine);
    build_flash_aml(dsdt, lams);
#ifdef CONFIG_TPM
    acpi_dsdt_add_tpm(dsdt, lams);
#endif
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
    build_pptt(tables_blob, tables->linker, machine,
               lams->oem_id, lams->oem_table_id);

    acpi_add_table(table_offsets, tables_blob);
    build_srat(tables_blob, tables->linker, machine);

    if (machine->numa_state->num_nodes) {
        if (machine->numa_state->have_numa_distance) {
            acpi_add_table(table_offsets, tables_blob);
            build_slit(tables_blob, tables->linker, machine, lams->oem_id,
                       lams->oem_table_id);
        }
        if (machine->numa_state->hmat_enabled) {
            acpi_add_table(table_offsets, tables_blob);
            build_hmat(tables_blob, tables->linker, machine->numa_state,
                       lams->oem_id, lams->oem_table_id);
        }
    }

    acpi_add_table(table_offsets, tables_blob);
    {
        AcpiMcfgInfo mcfg = {
           .base = cpu_to_le64(VIRT_PCI_CFG_BASE),
           .size = cpu_to_le64(VIRT_PCI_CFG_SIZE),
        };
        build_mcfg(tables_blob, tables->linker, &mcfg, lams->oem_id,
                   lams->oem_table_id);
    }

#ifdef CONFIG_TPM
    /* TPM info */
    if (tpm_get_version(tpm_find()) == TPM_VERSION_2_0) {
        acpi_add_table(table_offsets, tables_blob);
        build_tpm2(tables_blob, tables->linker,
                   tables->tcpalog, lams->oem_id,
                   lams->oem_table_id);
    }
#endif
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
