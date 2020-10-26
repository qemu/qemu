/*
 * Machine definitions for boards featuring an NPCM7xx SoC.
 *
 * Copyright 2020 Google LLC
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

#include "exec/address-spaces.h"
#include "hw/arm/npcm7xx.h"
#include "hw/core/cpu.h"
#include "hw/loader.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "qemu/units.h"
#include "sysemu/sysemu.h"

#define NPCM750_EVB_POWER_ON_STRAPS 0x00001ff7
#define QUANTA_GSJ_POWER_ON_STRAPS 0x00001fff

static const char npcm7xx_default_bootrom[] = "npcm7xx_bootrom.bin";

static void npcm7xx_load_bootrom(MachineState *machine, NPCM7xxState *soc)
{
    const char *bios_name = machine->firmware ?: npcm7xx_default_bootrom;
    g_autofree char *filename = NULL;
    int ret;

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    if (!filename) {
        error_report("Could not find ROM image '%s'", bios_name);
        if (!machine->kernel_filename) {
            /* We can't boot without a bootrom or a kernel image. */
            exit(1);
        }
        return;
    }
    ret = load_image_mr(filename, &soc->irom);
    if (ret < 0) {
        error_report("Failed to load ROM image '%s'", filename);
        exit(1);
    }
}

static void npcm7xx_connect_flash(NPCM7xxFIUState *fiu, int cs_no,
                                  const char *flash_type, DriveInfo *dinfo)
{
    DeviceState *flash;
    qemu_irq flash_cs;

    flash = qdev_new(flash_type);
    if (dinfo) {
        qdev_prop_set_drive(flash, "drive", blk_by_legacy_dinfo(dinfo));
    }
    qdev_realize_and_unref(flash, BUS(fiu->spi), &error_fatal);

    flash_cs = qdev_get_gpio_in_named(flash, SSI_GPIO_CS, 0);
    qdev_connect_gpio_out_named(DEVICE(fiu), "cs", cs_no, flash_cs);
}

static void npcm7xx_connect_dram(NPCM7xxState *soc, MemoryRegion *dram)
{
    memory_region_add_subregion(get_system_memory(), NPCM7XX_DRAM_BA, dram);

    object_property_set_link(OBJECT(soc), "dram-mr", OBJECT(dram),
                             &error_abort);
}

static NPCM7xxState *npcm7xx_create_soc(MachineState *machine,
                                        uint32_t hw_straps)
{
    NPCM7xxMachineClass *nmc = NPCM7XX_MACHINE_GET_CLASS(machine);
    MachineClass *mc = &nmc->parent;
    Object *obj;

    if (strcmp(machine->cpu_type, mc->default_cpu_type) != 0) {
        error_report("This board can only be used with %s",
                     mc->default_cpu_type);
        exit(1);
    }

    obj = object_new_with_props(nmc->soc_type, OBJECT(machine), "soc",
                                &error_abort, NULL);
    object_property_set_uint(obj, "power-on-straps", hw_straps, &error_abort);

    return NPCM7XX(obj);
}

static void npcm750_evb_init(MachineState *machine)
{
    NPCM7xxState *soc;

    soc = npcm7xx_create_soc(machine, NPCM750_EVB_POWER_ON_STRAPS);
    npcm7xx_connect_dram(soc, machine->ram);
    qdev_realize(DEVICE(soc), NULL, &error_fatal);

    npcm7xx_load_bootrom(machine, soc);
    npcm7xx_connect_flash(&soc->fiu[0], 0, "w25q256", drive_get(IF_MTD, 0, 0));
    npcm7xx_load_kernel(machine, soc);
}

static void quanta_gsj_init(MachineState *machine)
{
    NPCM7xxState *soc;

    soc = npcm7xx_create_soc(machine, QUANTA_GSJ_POWER_ON_STRAPS);
    npcm7xx_connect_dram(soc, machine->ram);
    qdev_realize(DEVICE(soc), NULL, &error_fatal);

    npcm7xx_load_bootrom(machine, soc);
    npcm7xx_connect_flash(&soc->fiu[0], 0, "mx25l25635e",
                          drive_get(IF_MTD, 0, 0));
    npcm7xx_load_kernel(machine, soc);
}

static void npcm7xx_set_soc_type(NPCM7xxMachineClass *nmc, const char *type)
{
    NPCM7xxClass *sc = NPCM7XX_CLASS(object_class_by_name(type));
    MachineClass *mc = MACHINE_CLASS(nmc);

    nmc->soc_type = type;
    mc->default_cpus = mc->min_cpus = mc->max_cpus = sc->num_cpus;
}

static void npcm7xx_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_parallel = 1;
    mc->default_ram_id = "ram";
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a9");
}

/*
 * Schematics:
 * https://github.com/Nuvoton-Israel/nuvoton-info/blob/master/npcm7xx-poleg/evaluation-board/board_deliverables/NPCM750x_EB_ver.A1.1_COMPLETE.pdf
 */
static void npcm750_evb_machine_class_init(ObjectClass *oc, void *data)
{
    NPCM7xxMachineClass *nmc = NPCM7XX_MACHINE_CLASS(oc);
    MachineClass *mc = MACHINE_CLASS(oc);

    npcm7xx_set_soc_type(nmc, TYPE_NPCM750);

    mc->desc = "Nuvoton NPCM750 Evaluation Board (Cortex A9)";
    mc->init = npcm750_evb_init;
    mc->default_ram_size = 512 * MiB;
};

static void gsj_machine_class_init(ObjectClass *oc, void *data)
{
    NPCM7xxMachineClass *nmc = NPCM7XX_MACHINE_CLASS(oc);
    MachineClass *mc = MACHINE_CLASS(oc);

    npcm7xx_set_soc_type(nmc, TYPE_NPCM730);

    mc->desc = "Quanta GSJ (Cortex A9)";
    mc->init = quanta_gsj_init;
    mc->default_ram_size = 512 * MiB;
};

static const TypeInfo npcm7xx_machine_types[] = {
    {
        .name           = TYPE_NPCM7XX_MACHINE,
        .parent         = TYPE_MACHINE,
        .instance_size  = sizeof(NPCM7xxMachine),
        .class_size     = sizeof(NPCM7xxMachineClass),
        .class_init     = npcm7xx_machine_class_init,
        .abstract       = true,
    }, {
        .name           = MACHINE_TYPE_NAME("npcm750-evb"),
        .parent         = TYPE_NPCM7XX_MACHINE,
        .class_init     = npcm750_evb_machine_class_init,
    }, {
        .name           = MACHINE_TYPE_NAME("quanta-gsj"),
        .parent         = TYPE_NPCM7XX_MACHINE,
        .class_init     = gsj_machine_class_init,
    },
};

DEFINE_TYPES(npcm7xx_machine_types)
