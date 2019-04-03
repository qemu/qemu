/*
 * Generic simulator target with no MMU or devices.  This emulation is
 * compatible with the libgloss qemu-hosted.ld linker script for using
 * QEMU as an instruction set simulator.
 *
 * Copyright (c) 2018-2019 Mentor Graphics
 *
 * Copyright (c) 2016 Marek Vasut <marek.vasut@gmail.com>
 *
 * Based on LabX device code
 *
 * Copyright (c) 2012 Chris Wulff <crwulff@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"

#include "hw/sysbus.h"
#include "hw/hw.h"
#include "hw/char/serial.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "qemu/config-file.h"

#include "boot.h"

#define BINARY_DEVICE_TREE_FILE    "generic-nommu.dtb"

static void nios2_generic_nommu_init(MachineState *machine)
{
    Nios2CPU *cpu;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *phys_tcm = g_new(MemoryRegion, 1);
    MemoryRegion *phys_tcm_alias = g_new(MemoryRegion, 1);
    MemoryRegion *phys_ram = g_new(MemoryRegion, 1);
    MemoryRegion *phys_ram_alias = g_new(MemoryRegion, 1);
    ram_addr_t tcm_base = 0x0;
    ram_addr_t tcm_size = 0x1000;    /* 1 kiB, but QEMU limit is 4 kiB */
    ram_addr_t ram_base = 0x10000000;
    ram_addr_t ram_size = 0x08000000;

    /* Physical TCM (tb_ram_1k) with alias at 0xc0000000 */
    memory_region_init_ram(phys_tcm, NULL, "nios2.tcm", tcm_size,
                           &error_abort);
    memory_region_init_alias(phys_tcm_alias, NULL, "nios2.tcm.alias",
                             phys_tcm, 0, tcm_size);
    memory_region_add_subregion(address_space_mem, tcm_base, phys_tcm);
    memory_region_add_subregion(address_space_mem, 0xc0000000 + tcm_base,
                                phys_tcm_alias);

    /* Physical DRAM with alias at 0xc0000000 */
    memory_region_init_ram(phys_ram, NULL, "nios2.ram", ram_size,
                           &error_abort);
    memory_region_init_alias(phys_ram_alias, NULL, "nios2.ram.alias",
                             phys_ram, 0, ram_size);
    memory_region_add_subregion(address_space_mem, ram_base, phys_ram);
    memory_region_add_subregion(address_space_mem, 0xc0000000 + ram_base,
                                phys_ram_alias);

    cpu = NIOS2_CPU(cpu_create(TYPE_NIOS2_CPU));

    /* Remove MMU */
    cpu->mmu_present = false;

    /* Reset vector is the first 32 bytes of RAM.  */
    cpu->reset_addr = ram_base;

    /* The interrupt vector comes right after reset.  */
    cpu->exception_addr = ram_base + 0x20;

    /*
     * The linker script does have a TLB miss memory region declared,
     * but this should never be used with no MMU.
     */
    cpu->fast_tlb_miss_addr = 0x7fff400;

    nios2_load_kernel(cpu, ram_base, ram_size, machine->initrd_filename,
                      BINARY_DEVICE_TREE_FILE, NULL);
}

static void nios2_generic_nommu_machine_init(struct MachineClass *mc)
{
    mc->desc = "Generic NOMMU Nios II design";
    mc->init = nios2_generic_nommu_init;
}

DEFINE_MACHINE("nios2-generic-nommu", nios2_generic_nommu_machine_init);
