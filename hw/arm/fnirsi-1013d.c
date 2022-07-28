/*
 * FNIRSI scope emulation
 *
 * Copyright (C) 2013 Li Guang
 * Written by Li Guang <lig.fnst@cn.fujitsu.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "sysemu/block-backend-io.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "exec/address-spaces.h"

#include "hw/arm/allwinner-f1.h"
#include "hw/gpio/fnirsi-1013d-pio.h"

static struct arm_boot_info fnirsi_binfo = {
    .loader_start = 0x00000000,
    .board_id     = 0x1009,
};

static void fnirsi_init(MachineState *machine)
{
    AwF1State *f1c100s;
    Error *err = NULL;
    DriveInfo *di;
    BlockBackend *blk;
    BusState *bus;
    DeviceState *carddev;

    /* BIOS is not supported by this board */
    if (machine->firmware) {
        error_report("BIOS not supported for this machine");
        exit(1);
    }

    /* This board has fixed size RAM (32MiB or 64MiB) */
    if (machine->ram_size != 32 * MiB) {
        error_report("This machine can only be used with 32MiB RAM");
        exit(1);
    }

    /* Only allow Cortex-A8 for this board */
    if (strcmp(machine->cpu_type, ARM_CPU_TYPE_NAME("arm926")) != 0) {
        error_report("This board can only be used with ARM926EJ-S CPU");
        exit(1);
    }

    f1c100s = AW_F1(object_new(TYPE_AW_F1));
    object_property_add_child(OBJECT(machine), "soc", OBJECT(f1c100s));
    object_unref(OBJECT(f1c100s));

    if (!object_property_set_int(OBJECT(&f1c100s->timer), "losc-clk", 32768, &err)) {
        error_reportf_err(err, "Couldn't set losc frequency: ");
        exit(1);
    }
    if (!object_property_set_int(OBJECT(&f1c100s->timer), "osc24m-clk", 24000000, &err)) {
        error_reportf_err(err, "Couldn't set osc24m frequency: ");
        exit(1);
    }

    if (!qdev_realize(DEVICE(f1c100s), NULL, &err)) {
        error_reportf_err(err, "Couldn't realize Allwinner F1100s: ");
        exit(1);
    }
    
    if (machine->enable_graphics) {
        fnirsi_tp_init(&f1c100s->pio);
    }
    
    fnirsi_fpga_init(&f1c100s->pio);
    // Some value in the middle of KEYADC range
    f1c100s->keyadc.adc_value = 40;  

    /* Retrieve SD bus */
    di = drive_get(IF_SD, 0, 0);
    blk = di ? blk_by_legacy_dinfo(di) : NULL;
    bus = qdev_get_child_bus(DEVICE(f1c100s), "sd-bus");

    /* Plug in SD card */
    carddev = qdev_new(TYPE_SD_CARD);
    qdev_prop_set_drive_err(carddev, "drive", blk, &error_fatal);
    qdev_realize_and_unref(carddev, bus, &error_fatal);

    memory_region_add_subregion(get_system_memory(), AW_F1_SDRAM_ADDR,
                                machine->ram);
    fnirsi_binfo.ram_size = machine->ram_size;
    
    if (!machine->kernel_filename) {
        if (blk && blk_is_available(blk)) {
            /* Start using SPL */
            /* Load SPL from SD card to SRAM */
#if 0 // Temporary           
            aw_f1_spl_setup(OBJECT(f1c100s), blk);
            fnirsi_binfo.loader_start = AW_F1_SRAM_ADDR;    
            //fnirsi_binfo.entry        = AW_F1_SRAM_ADDR;
        } else {
#endif        
            /* Start from BROM */
            MemoryRegion *boot_rom = g_new(MemoryRegion, 1);
            memory_region_init_rom(boot_rom, NULL, "f1c100s.bootrom",
                                   AW_F1_BROM_SIZE, &error_abort);
            memory_region_add_subregion(get_system_memory(), AW_F1_BROM_ADDR,
                                        boot_rom);
            aw_f1_bootrom_setup(OBJECT(f1c100s));            
            fnirsi_binfo.loader_start = AW_F1_BROM_ADDR;
            //fnirsi_binfo.entry        = AW_F1_BROM_ADDR;
        }
        fnirsi_binfo.firmware_loaded = true;
        f1c100s->cpu.env.boot_info = &fnirsi_binfo;
    } else {
        arm_load_kernel(&f1c100s->cpu, machine, &fnirsi_binfo);
        fnirsi_binfo.loader_start = AW_F1_SDRAM_ADDR;        
    }
}

static void fnirsi_machine_init(MachineClass *mc)
{
    mc->desc = "FNIRSI Scope (ARM926EJ-S)";
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm926");
    mc->default_ram_size = 32 * MiB;
    mc->init = fnirsi_init;
    mc->block_default_type = IF_SD;
    mc->units_per_default_bus = 1;
    //mc->ignore_memory_transaction_failures = true;
    mc->default_ram_id = "fnirsi.dram";
}

DEFINE_MACHINE("fnirsi", fnirsi_machine_init)
