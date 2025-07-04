/*
 * MAX78000FTHR Evaluation Board
 *
 * Copyright (c) 2025 Jackson Donaldson <jcksn@duck.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-clock.h"
#include "qemu/error-report.h"
#include "hw/arm/max78000_soc.h"
#include "hw/arm/boot.h"

/* 60MHz is the default, but other clocks can be selected. */
#define SYSCLK_FRQ 60000000ULL
static void max78000_init(MachineState *machine)
{
    DeviceState *dev;
    Clock *sysclk;

    sysclk = clock_new(OBJECT(machine), "SYSCLK");
    clock_set_hz(sysclk, SYSCLK_FRQ);

    dev = qdev_new(TYPE_MAX78000_SOC);
    object_property_add_child(OBJECT(machine), "soc", OBJECT(dev));
    qdev_connect_clock_in(dev, "sysclk", sysclk);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    armv7m_load_kernel(ARM_CPU(first_cpu),
                       machine->kernel_filename,
                       0x00000000, FLASH_SIZE);
}

static void max78000_machine_init(MachineClass *mc)
{
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-m4"),
        NULL
    };

    mc->desc = "MAX78000FTHR Board (Cortex-M4 / (Unimplemented) RISC-V)";
    mc->init = max78000_init;
    mc->valid_cpu_types = valid_cpu_types;
}

DEFINE_MACHINE("max78000fthr", max78000_machine_init)
