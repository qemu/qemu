/*
 * Axiado Boards
 *
 * Author: Kuan-Jui Chiu <kchiu@axiado.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/arm/ax3000-boards.h"
#include "hw/arm/boot.h"
#include "hw/arm/machines-qom.h"
#include "qemu/error-report.h"
#include "qom/object.h"

static struct arm_boot_info ax3000_binfo = {
    .loader_start = AX3000_DRAM0_BASE,
    .board_id = -1,
};

static void ax3000_machine_init(MachineState *machine)
{
    Ax3000MachineState *ams = AX3000_MACHINE(machine);

    ams->soc = AX3000_SOC(object_new(TYPE_AX3000_SOC));
    object_property_add_child(OBJECT(machine), "soc", OBJECT(ams->soc));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(ams->soc), &error_fatal);

    ax3000_binfo.ram_size = machine->ram_size;
    arm_load_kernel(&ams->soc->cpu[0], machine, &ax3000_binfo);
}

static void ax3000_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = ax3000_machine_init;
    mc->default_cpus = AX3000_NUM_CPUS;
    mc->min_cpus = AX3000_NUM_CPUS;
    mc->max_cpus = AX3000_NUM_CPUS;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a53");
}

static const TypeInfo ax3000_machine_types[] = {
    {
        .name          = TYPE_AX3000_MACHINE,
        .parent        = TYPE_MACHINE,
        .instance_size = sizeof(Ax3000MachineState),
        .class_size    = sizeof(Ax3000MachineClass),
        .class_init    = ax3000_machine_class_init,
        .interfaces    = aarch64_machine_interfaces,
        .abstract      = true,
    }
};

DEFINE_TYPES(ax3000_machine_types)
