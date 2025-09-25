/*
 * Test Machine for the IBM PPE42 processor
 *
 * Copyright (c) 2025, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "system/address-spaces.h"
#include "hw/boards.h"
#include "hw/ppc/ppc.h"
#include "system/system.h"
#include "system/reset.h"
#include "system/kvm.h"
#include "qapi/error.h"

#define TYPE_PPE42_MACHINE MACHINE_TYPE_NAME("ppe42_machine")
typedef MachineClass Ppe42MachineClass;
typedef struct Ppe42MachineState Ppe42MachineState;
DECLARE_OBJ_CHECKERS(Ppe42MachineState, Ppe42MachineClass,
                     PPE42_MACHINE, TYPE_PPE42_MACHINE)

struct Ppe42MachineState {
    MachineState parent_obj;

    PowerPCCPU cpu;
};

static void main_cpu_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
}

static void ppe42_machine_init(MachineState *machine)
{
    Ppe42MachineState *pms = PPE42_MACHINE(machine);
    PowerPCCPU *cpu = &pms->cpu;

    if (kvm_enabled()) {
        error_report("machine %s does not support the KVM accelerator",
                     MACHINE_GET_CLASS(machine)->name);
        exit(EXIT_FAILURE);
    }
    if (machine->ram_size > 512 * KiB) {
        error_report("RAM size more than 512 KiB is not supported");
        exit(1);
    }

    /* init CPU */
    object_initialize_child(OBJECT(pms), "cpu", cpu, machine->cpu_type);
    if (!qdev_realize(DEVICE(cpu), NULL, &error_fatal)) {
        return;
    }

    qemu_register_reset(main_cpu_reset, cpu);

    /* This sets the decrementer timebase */
    ppc_booke_timers_init(cpu, 37500000, PPC_TIMER_PPE);

    /* RAM */
    memory_region_add_subregion(get_system_memory(), 0xfff80000, machine->ram);
}


static void ppe42_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    static const char * const valid_cpu_types[] = {
        POWERPC_CPU_TYPE_NAME("PPE42"),
        POWERPC_CPU_TYPE_NAME("PPE42X"),
        POWERPC_CPU_TYPE_NAME("PPE42XM"),
        NULL,
    };

    mc->desc = "PPE42 Test Machine";
    mc->init = ppe42_machine_init;
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("PPE42XM");
    mc->valid_cpu_types = valid_cpu_types;
    mc->default_ram_id = "ram";
    mc->default_ram_size = 512 * KiB;
}

static const TypeInfo ppe42_machine_info = {
        .name          = TYPE_PPE42_MACHINE,
        .parent        = TYPE_MACHINE,
        .instance_size = sizeof(Ppe42MachineState),
        .class_init    = ppe42_machine_class_init,
        .class_size    = sizeof(Ppe42MachineClass),
};

static void ppe42_machine_register_types(void)
{
    type_register_static(&ppe42_machine_info);
}

type_init(ppe42_machine_register_types);
