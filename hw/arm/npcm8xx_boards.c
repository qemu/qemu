/*
 * Machine definitions for boards featuring an NPCM8xx SoC.
 *
 * Copyright 2021 Google LLC
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

#include "chardev/char.h"
#include "hw/boards.h"
#include "hw/arm/npcm8xx.h"
#include "hw/core/cpu.h"
#include "hw/loader.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/datadir.h"
#include "qemu/units.h"

#define NPCM845_EVB_POWER_ON_STRAPS 0x000017ff

static const char npcm8xx_default_bootrom[] = "npcm8xx_bootrom.bin";

static void npcm8xx_load_bootrom(MachineState *machine, NPCM8xxState *soc)
{
    const char *bios_name = machine->firmware ?: npcm8xx_default_bootrom;
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
    ret = load_image_mr(filename, machine->ram);
    if (ret < 0) {
        error_report("Failed to load ROM image '%s'", filename);
        exit(1);
    }
}

static void npcm8xx_connect_flash(NPCM7xxFIUState *fiu, int cs_no,
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

static void npcm8xx_connect_dram(NPCM8xxState *soc, MemoryRegion *dram)
{
    memory_region_add_subregion(get_system_memory(), NPCM8XX_DRAM_BA, dram);

    object_property_set_link(OBJECT(soc), "dram-mr", OBJECT(dram),
                             &error_abort);
}

static NPCM8xxState *npcm8xx_create_soc(MachineState *machine,
                                        uint32_t hw_straps)
{
    NPCM8xxMachineClass *nmc = NPCM8XX_MACHINE_GET_CLASS(machine);
    Object *obj;

    obj = object_new_with_props(nmc->soc_type, OBJECT(machine), "soc",
                                &error_abort, NULL);
    object_property_set_uint(obj, "power-on-straps", hw_straps, &error_abort);

    return NPCM8XX(obj);
}

static I2CBus *npcm8xx_i2c_get_bus(NPCM8xxState *soc, uint32_t num)
{
    g_assert(num < ARRAY_SIZE(soc->smbus));
    return I2C_BUS(qdev_get_child_bus(DEVICE(&soc->smbus[num]), "i2c-bus"));
}

static void npcm8xx_init_pwm_splitter(NPCM8xxMachine *machine,
                                      NPCM8xxState *soc, const int *fan_counts)
{
    SplitIRQ *splitters = machine->fan_splitter;

    /*
     * PWM 0~3 belong to module 0 output 0~3.
     * PWM 4~7 belong to module 1 output 0~3.
     */
    for (int i = 0; i < NPCM8XX_NR_PWM_MODULES; ++i) {
        for (int j = 0; j < NPCM7XX_PWM_PER_MODULE; ++j) {
            int splitter_no = i * NPCM7XX_PWM_PER_MODULE + j;
            DeviceState *splitter;

            if (fan_counts[splitter_no] < 1) {
                continue;
            }
            object_initialize_child(OBJECT(machine), "fan-splitter[*]",
                                    &splitters[splitter_no], TYPE_SPLIT_IRQ);
            splitter = DEVICE(&splitters[splitter_no]);
            qdev_prop_set_uint16(splitter, "num-lines",
                                 fan_counts[splitter_no]);
            qdev_realize(splitter, NULL, &error_abort);
            qdev_connect_gpio_out_named(DEVICE(&soc->pwm[i]), "duty-gpio-out",
                                        j, qdev_get_gpio_in(splitter, 0));
        }
    }
}

static void npcm8xx_connect_pwm_fan(NPCM8xxState *soc, SplitIRQ *splitter,
                                    int fan_no, int output_no)
{
    DeviceState *fan;
    int fan_input;
    qemu_irq fan_duty_gpio;

    g_assert(fan_no >= 0 && fan_no <= NPCM7XX_MFT_MAX_FAN_INPUT);
    /*
     * Fan 0~1 belong to module 0 input 0~1.
     * Fan 2~3 belong to module 1 input 0~1.
     * ...
     * Fan 14~15 belong to module 7 input 0~1.
     * Fan 16~17 belong to module 0 input 2~3.
     * Fan 18~19 belong to module 1 input 2~3.
     */
    if (fan_no < 16) {
        fan = DEVICE(&soc->mft[fan_no / 2]);
        fan_input = fan_no % 2;
    } else {
        fan = DEVICE(&soc->mft[(fan_no - 16) / 2]);
        fan_input = fan_no % 2 + 2;
    }

    /* Connect the Fan to PWM module */
    fan_duty_gpio = qdev_get_gpio_in_named(fan, "duty", fan_input);
    qdev_connect_gpio_out(DEVICE(splitter), output_no, fan_duty_gpio);
}

static void npcm845_evb_i2c_init(NPCM8xxState *soc)
{
    /* tmp100 temperature sensor on SVB, tmp105 is compatible */
    i2c_slave_create_simple(npcm8xx_i2c_get_bus(soc, 6), "tmp105", 0x48);
}

static void npcm845_evb_fan_init(NPCM8xxMachine *machine, NPCM8xxState *soc)
{
    SplitIRQ *splitter = machine->fan_splitter;
    static const int fan_counts[] = {2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0};

    npcm8xx_init_pwm_splitter(machine, soc, fan_counts);
    npcm8xx_connect_pwm_fan(soc, &splitter[0], 0x00, 0);
    npcm8xx_connect_pwm_fan(soc, &splitter[0], 0x01, 1);
    npcm8xx_connect_pwm_fan(soc, &splitter[1], 0x02, 0);
    npcm8xx_connect_pwm_fan(soc, &splitter[1], 0x03, 1);
    npcm8xx_connect_pwm_fan(soc, &splitter[2], 0x04, 0);
    npcm8xx_connect_pwm_fan(soc, &splitter[2], 0x05, 1);
    npcm8xx_connect_pwm_fan(soc, &splitter[3], 0x06, 0);
    npcm8xx_connect_pwm_fan(soc, &splitter[3], 0x07, 1);
    npcm8xx_connect_pwm_fan(soc, &splitter[4], 0x08, 0);
    npcm8xx_connect_pwm_fan(soc, &splitter[4], 0x09, 1);
    npcm8xx_connect_pwm_fan(soc, &splitter[5], 0x0a, 0);
    npcm8xx_connect_pwm_fan(soc, &splitter[5], 0x0b, 1);
    npcm8xx_connect_pwm_fan(soc, &splitter[6], 0x0c, 0);
    npcm8xx_connect_pwm_fan(soc, &splitter[6], 0x0d, 1);
    npcm8xx_connect_pwm_fan(soc, &splitter[7], 0x0e, 0);
    npcm8xx_connect_pwm_fan(soc, &splitter[7], 0x0f, 1);
}

static void npcm845_evb_init(MachineState *machine)
{
    NPCM8xxState *soc;

    soc = npcm8xx_create_soc(machine, NPCM845_EVB_POWER_ON_STRAPS);
    npcm8xx_connect_dram(soc, machine->ram);
    qdev_realize(DEVICE(soc), NULL, &error_fatal);

    npcm8xx_load_bootrom(machine, soc);
    npcm8xx_connect_flash(&soc->fiu[0], 0, "w25q256", drive_get(IF_MTD, 0, 0));
    npcm845_evb_i2c_init(soc);
    npcm845_evb_fan_init(NPCM8XX_MACHINE(machine), soc);
    npcm8xx_load_kernel(machine, soc);
}

static void npcm8xx_set_soc_type(NPCM8xxMachineClass *nmc, const char *type)
{
    NPCM8xxClass *sc = NPCM8XX_CLASS(object_class_by_name(type));
    MachineClass *mc = MACHINE_CLASS(nmc);

    nmc->soc_type = type;
    mc->default_cpus = mc->min_cpus = mc->max_cpus = sc->num_cpus;
}

static void npcm8xx_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-a9"),
        NULL
    };

    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_parallel = 1;
    mc->default_ram_id = "ram";
    mc->valid_cpu_types = valid_cpu_types;
}

static void npcm845_evb_machine_class_init(ObjectClass *oc, void *data)
{
    NPCM8xxMachineClass *nmc = NPCM8XX_MACHINE_CLASS(oc);
    MachineClass *mc = MACHINE_CLASS(oc);

    npcm8xx_set_soc_type(nmc, TYPE_NPCM8XX);

    mc->desc = "Nuvoton NPCM845 Evaluation Board (Cortex-A35)";
    mc->init = npcm845_evb_init;
    mc->default_ram_size = 1 * GiB;
};

static const TypeInfo npcm8xx_machine_types[] = {
    {
        .name           = TYPE_NPCM8XX_MACHINE,
        .parent         = TYPE_MACHINE,
        .instance_size  = sizeof(NPCM8xxMachine),
        .class_size     = sizeof(NPCM8xxMachineClass),
        .class_init     = npcm8xx_machine_class_init,
        .abstract       = true,
    }, {
        .name           = MACHINE_TYPE_NAME("npcm845-evb"),
        .parent         = TYPE_NPCM8XX_MACHINE,
        .class_init     = npcm845_evb_machine_class_init,
    },
};

DEFINE_TYPES(npcm8xx_machine_types)
