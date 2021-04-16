/*
 * Infineon TriBoard System emulation.
 *
 * Copyright (c) 2020 Andreas Konopik <andreas.konopik@efs-auto.de>
 * Copyright (c) 2020 David Brenken <david.brenken@efs-auto.de>
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
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "net/net.h"
#include "hw/loader.h"
#include "elf.h"
#include "hw/tricore/tricore.h"
#include "qemu/error-report.h"

#include "hw/tricore/triboard.h"
#include "hw/tricore/tc27x_soc.h"

static void tricore_load_kernel(const char *kernel_filename)
{
    uint64_t entry;
    long kernel_size;
    TriCoreCPU *cpu;
    CPUTriCoreState *env;

    kernel_size = load_elf(kernel_filename, NULL,
                           NULL, NULL, &entry, NULL,
                           NULL, NULL, 0,
                           EM_TRICORE, 1, 0);
    if (kernel_size <= 0) {
        error_report("no kernel file '%s'", kernel_filename);
        exit(1);
    }
    cpu = TRICORE_CPU(first_cpu);
    env = &cpu->env;
    env->PC = entry;
}


static void triboard_machine_init(MachineState *machine)
{
    TriBoardMachineState *ms = TRIBOARD_MACHINE(machine);
    TriBoardMachineClass *amc = TRIBOARD_MACHINE_GET_CLASS(machine);

    object_initialize_child(OBJECT(machine), "soc", &ms->tc27x_soc,
            amc->soc_name);
    sysbus_realize(SYS_BUS_DEVICE(&ms->tc27x_soc), &error_fatal);

    if (machine->kernel_filename) {
        tricore_load_kernel(machine->kernel_filename);
    }
}

static void triboard_machine_tc277d_class_init(ObjectClass *oc,
        void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    TriBoardMachineClass *amc = TRIBOARD_MACHINE_CLASS(oc);

    mc->init        = triboard_machine_init;
    mc->desc        = "Infineon AURIX TriBoard TC277 (D-Step)";
    mc->max_cpus    = 1;
    amc->soc_name   = "tc277d-soc";
};

static const TypeInfo triboard_machine_types[] = {
    {
        .name           = MACHINE_TYPE_NAME("KIT_AURIX_TC277_TRB"),
        .parent         = TYPE_TRIBOARD_MACHINE,
        .class_init     = triboard_machine_tc277d_class_init,
    }, {
        .name           = TYPE_TRIBOARD_MACHINE,
        .parent         = TYPE_MACHINE,
        .instance_size  = sizeof(TriBoardMachineState),
        .class_size     = sizeof(TriBoardMachineClass),
        .abstract       = true,
    },
};

DEFINE_TYPES(triboard_machine_types)
