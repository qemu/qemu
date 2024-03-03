/*
 * B-L475E-IOT01A Discovery Kit machine
 * (B-L475E-IOT01A IoT Node)
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * This work is heavily inspired by the netduinoplus2 by Alistair Francis.
 * Original code is licensed under the MIT License:
 *
 * Copyright (c) 2014 Alistair Francis <alistair@alistair23.me>
 */

/*
 * The reference used is the STMicroElectronics UM2153 User manual
 * Discovery kit for IoT node, multi-channel communication with STM32L4.
 * https://www.st.com/en/evaluation-tools/b-l475e-iot01a.html#documentation
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "qemu/error-report.h"
#include "hw/arm/stm32l4x5_soc.h"
#include "hw/arm/boot.h"

/* B-L475E-IOT01A implementation is derived from netduinoplus2 */

static void b_l475e_iot01a_init(MachineState *machine)
{
    const Stm32l4x5SocClass *sc;
    DeviceState *dev;

    dev = qdev_new(TYPE_STM32L4X5XG_SOC);
    object_property_add_child(OBJECT(machine), "soc", OBJECT(dev));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    sc = STM32L4X5_SOC_GET_CLASS(dev);
    armv7m_load_kernel(ARM_CPU(first_cpu),
                       machine->kernel_filename,
                       0, sc->flash_size);
}

static void b_l475e_iot01a_machine_init(MachineClass *mc)
{
    static const char *machine_valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-m4"),
        NULL
    };
    mc->desc = "B-L475E-IOT01A Discovery Kit (Cortex-M4)";
    mc->init = b_l475e_iot01a_init;
    mc->valid_cpu_types = machine_valid_cpu_types;

    /* SRAM pre-allocated as part of the SoC instantiation */
    mc->default_ram_size = 0;
}

DEFINE_MACHINE("b-l475e-iot01a", b_l475e_iot01a_machine_init)
