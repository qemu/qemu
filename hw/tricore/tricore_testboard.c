/*
 * TriCore Baseboard System emulation.
 *
 * Copyright (c) 2013-2014 Bastian Koppelmann C-Lab/University Paderborn
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */


#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/hw.h"
#include "hw/devices.h"
#include "net/net.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "sysemu/block-backend.h"
#include "exec/address-spaces.h"
#include "hw/block/flash.h"
#include "elf.h"
#include "hw/tricore/tricore.h"
#include "qemu/error-report.h"


/* Board init.  */

static struct tricore_boot_info tricoretb_binfo;

static void tricore_load_kernel(CPUTriCoreState *env)
{
    uint64_t entry;
    long kernel_size;

    kernel_size = load_elf(tricoretb_binfo.kernel_filename, NULL,
                           NULL, &entry, NULL,
                           NULL, 0,
                           EM_TRICORE, 1, 0);
    if (kernel_size <= 0) {
        error_report("qemu: no kernel file '%s'",
                tricoretb_binfo.kernel_filename);
        exit(1);
    }
    env->PC = entry;

}

static void tricore_testboard_init(MachineState *machine, int board_id)
{
    TriCoreCPU *cpu;
    CPUTriCoreState *env;

    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *ext_cram = g_new(MemoryRegion, 1);
    MemoryRegion *ext_dram = g_new(MemoryRegion, 1);
    MemoryRegion *int_cram = g_new(MemoryRegion, 1);
    MemoryRegion *int_dram = g_new(MemoryRegion, 1);
    MemoryRegion *pcp_data = g_new(MemoryRegion, 1);
    MemoryRegion *pcp_text = g_new(MemoryRegion, 1);

    if (!machine->cpu_model) {
        machine->cpu_model = "tc1796";
    }
    cpu = cpu_tricore_init(machine->cpu_model);
    if (!cpu) {
        error_report("Unable to find CPU definition");
        exit(1);
    }
    env = &cpu->env;
    memory_region_init_ram(ext_cram, NULL, "powerlink_ext_c.ram", 2*1024*1024,
                           &error_fatal);
    vmstate_register_ram_global(ext_cram);
    memory_region_init_ram(ext_dram, NULL, "powerlink_ext_d.ram", 4*1024*1024,
                           &error_fatal);
    vmstate_register_ram_global(ext_dram);
    memory_region_init_ram(int_cram, NULL, "powerlink_int_c.ram", 48*1024,
                           &error_fatal);
    vmstate_register_ram_global(int_cram);
    memory_region_init_ram(int_dram, NULL, "powerlink_int_d.ram", 48*1024,
                           &error_fatal);
    vmstate_register_ram_global(int_dram);
    memory_region_init_ram(pcp_data, NULL, "powerlink_pcp_data.ram", 16*1024,
                           &error_fatal);
    vmstate_register_ram_global(pcp_data);
    memory_region_init_ram(pcp_text, NULL, "powerlink_pcp_text.ram", 32*1024,
                           &error_fatal);
    vmstate_register_ram_global(pcp_text);

    memory_region_add_subregion(sysmem, 0x80000000, ext_cram);
    memory_region_add_subregion(sysmem, 0xa1000000, ext_dram);
    memory_region_add_subregion(sysmem, 0xd4000000, int_cram);
    memory_region_add_subregion(sysmem, 0xd0000000, int_dram);
    memory_region_add_subregion(sysmem, 0xf0050000, pcp_data);
    memory_region_add_subregion(sysmem, 0xf0060000, pcp_text);

    tricoretb_binfo.ram_size = machine->ram_size;
    tricoretb_binfo.kernel_filename = machine->kernel_filename;

    if (machine->kernel_filename) {
        tricore_load_kernel(env);
    }
}

static void tricoreboard_init(MachineState *machine)
{
    tricore_testboard_init(machine, 0x183);
}

static void ttb_machine_init(MachineClass *mc)
{
    mc->desc = "a minimal TriCore board";
    mc->init = tricoreboard_init;
    mc->is_default = 0;
}

DEFINE_MACHINE("tricore_testboard", ttb_machine_init)
