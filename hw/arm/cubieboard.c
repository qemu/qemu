/*
 * cubieboard emulation
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
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "hw/arm/allwinner-a10.h"
#include "hw/arm/boot.h"
#include "hw/i2c/i2c.h"

static struct arm_boot_info cubieboard_binfo = {
    .loader_start = AW_A10_SDRAM_BASE,
    .board_id = 0x1008,
};

static void cubieboard_init(MachineState *machine)
{
    AwA10State *a10;
    Error *err = NULL;
    DriveInfo *di;
    BlockBackend *blk;
    BusState *bus;
    DeviceState *carddev;
    I2CBus *i2c;

    /* BIOS is not supported by this board */
    if (machine->firmware) {
        error_report("BIOS not supported for this machine");
        exit(1);
    }

    /* This board has fixed size RAM (512MiB or 1GiB) */
    if (machine->ram_size != 512 * MiB &&
        machine->ram_size != 1 * GiB) {
        error_report("This machine can only be used with 512MiB or 1GiB RAM");
        exit(1);
    }

    a10 = AW_A10(object_new(TYPE_AW_A10));
    object_property_add_child(OBJECT(machine), "soc", OBJECT(a10));
    object_unref(OBJECT(a10));

    if (!object_property_set_int(OBJECT(&a10->emac), "phy-addr", 1, &err)) {
        error_reportf_err(err, "Couldn't set phy address: ");
        exit(1);
    }

    if (!object_property_set_int(OBJECT(&a10->timer), "clk0-freq", 32768,
                                 &err)) {
        error_reportf_err(err, "Couldn't set clk0 frequency: ");
        exit(1);
    }

    if (!object_property_set_int(OBJECT(&a10->timer), "clk1-freq", 24000000,
                                 &err)) {
        error_reportf_err(err, "Couldn't set clk1 frequency: ");
        exit(1);
    }

    if (!qdev_realize(DEVICE(a10), NULL, &err)) {
        error_reportf_err(err, "Couldn't realize Allwinner A10: ");
        exit(1);
    }

    /* Connect AXP 209 */
    i2c = I2C_BUS(qdev_get_child_bus(DEVICE(&a10->i2c0), "i2c"));
    i2c_slave_create_simple(i2c, "axp209_pmu", 0x34);

    /* Retrieve SD bus */
    di = drive_get(IF_SD, 0, 0);
    blk = di ? blk_by_legacy_dinfo(di) : NULL;
    bus = qdev_get_child_bus(DEVICE(a10), "sd-bus");

    /* Plug in SD card */
    carddev = qdev_new(TYPE_SD_CARD);
    qdev_prop_set_drive_err(carddev, "drive", blk, &error_fatal);
    qdev_realize_and_unref(carddev, bus, &error_fatal);

    memory_region_add_subregion(get_system_memory(), AW_A10_SDRAM_BASE,
                                machine->ram);

    /* Load target kernel or start using BootROM */
    if (!machine->kernel_filename && blk && blk_is_available(blk)) {
        /* Use Boot ROM to copy data from SD card to SRAM */
        allwinner_a10_bootrom_setup(a10, blk);
    }
    /* TODO create and connect IDE devices for ide_drive_get() */

    cubieboard_binfo.ram_size = machine->ram_size;
    arm_load_kernel(&a10->cpu, machine, &cubieboard_binfo);
}

static void cubieboard_machine_init(MachineClass *mc)
{
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-a8"),
        NULL
    };

    mc->desc = "cubietech cubieboard (Cortex-A8)";
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a8");
    mc->valid_cpu_types = valid_cpu_types;
    mc->default_ram_size = 1 * GiB;
    mc->init = cubieboard_init;
    mc->block_default_type = IF_IDE;
    mc->units_per_default_bus = 1;
    mc->ignore_memory_transaction_failures = true;
    mc->default_ram_id = "cubieboard.ram";
    mc->auto_create_sdcard = true;
}

DEFINE_MACHINE("cubieboard", cubieboard_machine_init)
