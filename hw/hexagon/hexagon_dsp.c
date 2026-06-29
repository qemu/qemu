/*
 * Hexagon DSP Subsystem emulation.  This represents a generic DSP
 * subsystem with few peripherals, like the Compute DSP.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */


#include "qemu/osdep.h"
#include "qemu/units.h"
#include "system/address-spaces.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-properties.h"
#include "hw/hexagon/hexagon.h"
#include "hw/hexagon/hexagon_globalreg.h"
#include "hw/hexagon/hexagon_tlb.h"
#include "hw/core/loader.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "elf.h"
#include "cpu.h"
#include "migration/cpu.h"
#include "system/system.h"
#include "target/hexagon/internal.h"
#include "system/physmem.h"
#include "system/reset.h"

#include "machine_cfg_v66g_1024.h.inc"

#define TYPE_HEXAGON_DSP_MACHINE "hexagon-dsp-machine"
OBJECT_DECLARE_SIMPLE_TYPE(HexagonDspMachineState, HEXAGON_DSP_MACHINE)

struct HexagonDspMachineState {
    HexagonCommonMachineState parent_obj;

    hwaddr isdb_secure_flag;
    hwaddr isdb_trusted_flag;
};

static HexagonDspMachineState *current_dms;

static void hex_symbol_callback(const char *st_name, int st_info,
                                uint64_t st_value, uint64_t st_size)
{
    if (!g_strcmp0("isdb_secure_flag", st_name)) {
        current_dms->isdb_secure_flag = st_value;
    }
    if (!g_strcmp0("isdb_trusted_flag", st_name)) {
        current_dms->isdb_trusted_flag = st_value;
    }
}

/* Board init.  */
static struct hexagon_board_boot_info hexagon_binfo;

static void hexagon_load_kernel(HexagonDspMachineState *dms, HexagonCPU *cpu)
{
    uint64_t pentry;
    long kernel_size;

    current_dms = dms;
    kernel_size = load_elf_ram_sym(hexagon_binfo.kernel_filename, NULL, NULL,
                      NULL, &pentry, NULL, NULL,
                      &hexagon_binfo.kernel_elf_flags, 0, EM_HEXAGON, 0, 0,
                      &address_space_memory, false, hex_symbol_callback);
    current_dms = NULL;

    if (kernel_size <= 0) {
        error_report("no kernel file '%s'",
            hexagon_binfo.kernel_filename);
        exit(1);
    }

    qdev_prop_set_uint32(DEVICE(cpu), "exec-start-addr", pentry);
}

static void hexagon_init_bootstrap(HexagonDspMachineState *dms, HexagonCPU *cpu)
{
    MachineState *machine = MACHINE(dms);

    if (machine->kernel_filename) {
        uint32_t mem = 1;

        hexagon_load_kernel(dms, cpu);
        if (dms->isdb_secure_flag) {
            physical_memory_write(dms->isdb_secure_flag,
                                     &mem, sizeof(mem));
        }
        if (dms->isdb_trusted_flag) {
            physical_memory_write(dms->isdb_trusted_flag,
                                     &mem, sizeof(mem));
        }
    }
}

static void do_cpu_reset(void *opaque)
{
    HexagonCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    cpu_reset(cs);
}

static void hexagon_common_init(MachineState *machine, Rev_t rev,
                                const struct hexagon_machine_config *m_cfg)
{
    HexagonCommonMachineState *hms = HEXAGON_COMMON_MACHINE(machine);
    HexagonDspMachineState *dms = HEXAGON_DSP_MACHINE(machine);
    MemoryRegion *address_space;
    DeviceState *glob_regs_dev;
    DeviceState *tlb_dev;

    memset(&hexagon_binfo, 0, sizeof(hexagon_binfo));
    if (machine->kernel_filename) {
        hexagon_binfo.ram_size = machine->ram_size;
        hexagon_binfo.kernel_filename = machine->kernel_filename;
    }

    machine->enable_graphics = 0;

    address_space = get_system_memory();

    memory_region_init_rom(&hms->cfgtable_rom, NULL, "config_table.rom",
                           sizeof(m_cfg->cfgtable), &error_fatal);
    memory_region_add_subregion(address_space, m_cfg->cfgbase,
                                &hms->cfgtable_rom);

    memory_region_init_ram(&hms->ram, NULL, "ddr.ram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(address_space, 0x0, &hms->ram);

    glob_regs_dev = qdev_new(TYPE_HEXAGON_GLOBALREG);
    object_property_add_child(OBJECT(machine), "global-regs",
                              OBJECT(glob_regs_dev));
    qdev_prop_set_uint64(glob_regs_dev, "config-table-addr", m_cfg->cfgbase);
    qdev_prop_set_uint32(glob_regs_dev, "dsp-rev", rev);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(glob_regs_dev), &error_fatal);

    tlb_dev = qdev_new(TYPE_HEXAGON_TLB);
    object_property_add_child(OBJECT(machine), "tlb", OBJECT(tlb_dev));
    qdev_prop_set_uint32(tlb_dev, "num-entries",
                         m_cfg->cfgtable.jtlb_size_entries);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(tlb_dev), &error_fatal);

    for (int i = 0; i < machine->smp.cpus; i++) {
        HexagonCPU *cpu = HEXAGON_CPU(object_new(machine->cpu_type));
        qemu_register_reset(do_cpu_reset, cpu);

        /*
         * CPU #0 is the only CPU running at boot, others must be
         * explicitly enabled via start instruction.
         */
        qdev_prop_set_bit(DEVICE(cpu), "start-powered-off", (i != 0));
        if (i == 0) {
            hexagon_init_bootstrap(dms, cpu);
        }
        object_property_set_link(OBJECT(cpu), "global-regs",
                                 OBJECT(glob_regs_dev), &error_fatal);
        object_property_set_link(OBJECT(cpu), "tlb",
                                 OBJECT(tlb_dev), &error_fatal);
        qdev_realize_and_unref(DEVICE(cpu), NULL, &error_fatal);
    }

    rom_add_blob_fixed_as("config_table.rom", &m_cfg->cfgtable,
                          sizeof(m_cfg->cfgtable), m_cfg->cfgbase,
                          &address_space_memory);
}

static void init_mc(MachineClass *mc)
{
    mc->block_default_type = IF_SD;
    mc->default_ram_size = 4 * GiB;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_serial = 1;
    mc->is_default = false;
    mc->max_cpus = 8;
}

/* ----------------------------------------------------------------- */
/* Core-specific configuration settings are defined below this line. */
/* Config table values defined in machine_configs.h.inc              */
/* ----------------------------------------------------------------- */

static void v66g_1024_config_init(MachineState *machine)
{
    hexagon_common_init(machine, v66_rev, &v66g_1024);
}

static void v66g_1024_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Hexagon V66G_1024";
    mc->init = v66g_1024_config_init;
    init_mc(mc);
    mc->is_default = true;
    mc->default_cpu_type = TYPE_HEXAGON_CPU_V66;
    mc->default_cpus = 4;
}

static const TypeInfo hexagon_machine_types[] = {
    {
        .name = TYPE_HEXAGON_COMMON_MACHINE,
        .parent = TYPE_MACHINE,
        .instance_size = sizeof(HexagonCommonMachineState),
        .abstract = true,
    },
    {
        .name = TYPE_HEXAGON_DSP_MACHINE,
        .parent = TYPE_HEXAGON_COMMON_MACHINE,
        .instance_size = sizeof(HexagonDspMachineState),
        .abstract = true,
    },
    {
        .name = MACHINE_TYPE_NAME("V66G_1024"),
        .parent = TYPE_HEXAGON_DSP_MACHINE,
        .class_init = v66g_1024_init,
    },
};

DEFINE_TYPES(hexagon_machine_types)
