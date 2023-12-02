/*
 * Hexagon DSP Subsystem emulation.  This represents a generic DSP
 * subsystem with few peripherals, like the Compute DSP.
 *
 * Copyright (c) 2020-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */


#include "qemu/osdep.h"
#include "qemu/units.h"
#include "exec/address-spaces.h"
#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "hw/hexagon/hexagon.h"
#include "hw/loader.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "elf.h"
#include "cpu.h"
#include "include/migration/cpu.h"
#include "include/sysemu/sysemu.h"
#include "target/hexagon/internal.h"
#include "sysemu/reset.h"

#include "machine_configs.h.inc"

static void hex_symbol_callback(const char *st_name, int st_info,
                                uint64_t st_value, uint64_t st_size)
{
}

/* Board init.  */
static struct hexagon_board_boot_info hexagon_binfo;

static void hexagon_load_kernel(HexagonCPU *cpu)
{
    uint64_t pentry;
    long kernel_size;

    kernel_size = load_elf_ram_sym(hexagon_binfo.kernel_filename, NULL, NULL,
                      NULL, &pentry, NULL, NULL,
                      &hexagon_binfo.kernel_elf_flags, 0, EM_HEXAGON, 0, 0,
                      &address_space_memory, false, hex_symbol_callback);

    if (kernel_size <= 0) {
        error_report("no kernel file '%s'",
            hexagon_binfo.kernel_filename);
        exit(1);
    }

    qdev_prop_set_uint32(DEVICE(cpu), "exec-start-addr", pentry);
}

static void hexagon_init_bootstrap(MachineState *machine, HexagonCPU *cpu)
{
    if (machine->kernel_filename) {
        hexagon_load_kernel(cpu);
    }
}

static void do_cpu_reset(void *opaque)
{
    HexagonCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    cpu_reset(cs);
}

static void hexagon_common_init(MachineState *machine, Rev_t rev,
                                hexagon_machine_config *m_cfg)
{
    memset(&hexagon_binfo, 0, sizeof(hexagon_binfo));
    if (machine->kernel_filename) {
        hexagon_binfo.ram_size = machine->ram_size;
        hexagon_binfo.kernel_filename = machine->kernel_filename;
    }

    machine->enable_graphics = 0;

    MemoryRegion *address_space = get_system_memory();

    MemoryRegion *sram = g_new(MemoryRegion, 1);
    memory_region_init_ram(sram, NULL, "ddr.ram",
        machine->ram_size, &error_fatal);
    memory_region_add_subregion(address_space, 0x0, sram);

    Error **errp = NULL;

    for (int i = 0; i < machine->smp.cpus; i++) {
        HexagonCPU *cpu = HEXAGON_CPU(object_new(machine->cpu_type));
        qemu_register_reset(do_cpu_reset, cpu);

        /*
         * CPU #0 is the only CPU running at boot, others must be
         * explicitly enabled via start instruction.
         */
        qdev_prop_set_bit(DEVICE(cpu), "start-powered-off", (i != 0));

        if (i == 0) {
            hexagon_init_bootstrap(machine, cpu);
        }

        if (!qdev_realize_and_unref(DEVICE(cpu), NULL, errp)) {
            return;
        }
    }
}

static void init_mc(MachineClass *mc)
{
    mc->block_default_type = IF_SD;
    mc->default_ram_size = 4 * GiB;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_serial = 1;
    mc->no_sdcard = 1;
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

static void v66g_1024_init(ObjectClass *oc, void *data)
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
        .name = MACHINE_TYPE_NAME("V66G_1024"),
        .parent = TYPE_MACHINE,
        .class_init = v66g_1024_init,
    },
};

DEFINE_TYPES(hexagon_machine_types)
