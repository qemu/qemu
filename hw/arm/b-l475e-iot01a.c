/*
 * B-L475E-IOT01A Discovery Kit machine
 * (B-L475E-IOT01A IoT Node)
 *
 * Copyright (c) 2023-2024 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023-2024 Inès Varhol <ines.varhol@telecom-paris.fr>
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

#define TYPE_B_L475E_IOT01A MACHINE_TYPE_NAME("b-l475e-iot01a")
OBJECT_DECLARE_SIMPLE_TYPE(Bl475eMachineState, B_L475E_IOT01A)

typedef struct Bl475eMachineState {
    MachineState parent_obj;

    Stm32l4x5SocState soc;
} Bl475eMachineState;

static void bl475e_init(MachineState *machine)
{
    Bl475eMachineState *s = B_L475E_IOT01A(machine);
    const Stm32l4x5SocClass *sc;

    object_initialize_child(OBJECT(machine), "soc", &s->soc,
                            TYPE_STM32L4X5XG_SOC);
    sysbus_realize(SYS_BUS_DEVICE(&s->soc), &error_fatal);

    sc = STM32L4X5_SOC_GET_CLASS(&s->soc);
    armv7m_load_kernel(ARM_CPU(first_cpu), machine->kernel_filename, 0,
                       sc->flash_size);
}

static void bl475e_machine_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    static const char *machine_valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-m4"),
        NULL
    };
    mc->desc = "B-L475E-IOT01A Discovery Kit (Cortex-M4)";
    mc->init = bl475e_init;
    mc->valid_cpu_types = machine_valid_cpu_types;

    /* SRAM pre-allocated as part of the SoC instantiation */
    mc->default_ram_size = 0;
}

static const TypeInfo bl475e_machine_type[] = {
    {
        .name           = TYPE_B_L475E_IOT01A,
        .parent         = TYPE_MACHINE,
        .instance_size  = sizeof(Bl475eMachineState),
        .class_init     = bl475e_machine_init,
    }
};

DEFINE_TYPES(bl475e_machine_type)
