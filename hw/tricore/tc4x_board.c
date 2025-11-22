/*
 * Infineon TC4x Board emulation.
 *
 * Copyright (c) 2024 QEMU contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/loader.h"
#include "elf.h"
#include "hw/tricore/tricore.h"
#include "qemu/error-report.h"

#include "hw/tricore/tc4x_board.h"
#include "hw/tricore/tc4x_soc.h"

/*
 * Load kernel ELF file and set CPU entry point
 */
static void tc4x_load_kernel(TriCoreCPU *cpu, const char *kernel_filename)
{
    uint64_t entry;
    long kernel_size;
    CPUTriCoreState *env;

    kernel_size = load_elf(kernel_filename, NULL,
                           NULL, NULL, &entry, NULL,
                           NULL, NULL, ELFDATA2LSB,
                           EM_TRICORE, 1, 0);
    if (kernel_size <= 0) {
        error_report("Unable to load kernel file '%s'", kernel_filename);
        exit(1);
    }

    env = &cpu->env;
    env->PC = entry;
}

static void tc4x_machine_init(MachineState *machine)
{
    TC4xMachineState *ms = TC4X_MACHINE(machine);
    TC4xMachineClass *amc = TC4X_MACHINE_GET_CLASS(machine);

    /* Initialize the SoC */
    object_initialize_child(OBJECT(machine), "soc", &ms->soc, amc->soc_name);
    sysbus_realize(SYS_BUS_DEVICE(&ms->soc), &error_fatal);

    /* Load kernel if provided */
    if (machine->kernel_filename) {
        tc4x_load_kernel(&ms->soc.cpu, machine->kernel_filename);
    }
}

/*
 * TC4D7 machine - high-end TC4xx with 6 cores
 * This is suitable for running FreeRTOS, Zephyr, and AUTOSAR
 */
static void tc4d7_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    TC4xMachineClass *amc = TC4X_MACHINE_CLASS(oc);

    mc->init        = tc4x_machine_init;
    mc->desc        = "Infineon AURIX TC4D7 TriCore 1.8";
    mc->max_cpus    = 1;  /* Single-core emulation for now */
    amc->soc_name   = "tc4d7-soc";
}

static const TypeInfo tc4x_machine_types[] = {
    {
        .name           = MACHINE_TYPE_NAME("tc4d7"),
        .parent         = TYPE_TC4X_MACHINE,
        .class_init     = tc4d7_machine_class_init,
    },
    {
        .name           = TYPE_TC4X_MACHINE,
        .parent         = TYPE_MACHINE,
        .instance_size  = sizeof(TC4xMachineState),
        .class_size     = sizeof(TC4xMachineClass),
        .abstract       = true,
    },
};

DEFINE_TYPES(tc4x_machine_types)
