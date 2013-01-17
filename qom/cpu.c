/*
 * QEMU CPU model
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */

#include "qom/cpu.h"
#include "qemu-common.h"

void cpu_reset_interrupt(CPUState *cpu, int mask)
{
    cpu->interrupt_request &= ~mask;
}

void cpu_reset(CPUState *cpu)
{
    CPUClass *klass = CPU_GET_CLASS(cpu);

    if (klass->reset != NULL) {
        (*klass->reset)(cpu);
    }
}

static void cpu_common_reset(CPUState *cpu)
{
    cpu->exit_request = 0;
    cpu->interrupt_request = 0;
    cpu->current_tb = NULL;
    cpu->halted = 0;
}

ObjectClass *cpu_class_by_name(const char *typename, const char *cpu_model)
{
    CPUClass *cc = CPU_CLASS(object_class_by_name(typename));

    return cc->class_by_name(cpu_model);
}

static ObjectClass *cpu_common_class_by_name(const char *cpu_model)
{
    return NULL;
}

static void cpu_common_realizefn(DeviceState *dev, Error **errp)
{
}

static void cpu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    CPUClass *k = CPU_CLASS(klass);

    k->class_by_name = cpu_common_class_by_name;
    k->reset = cpu_common_reset;
    dc->realize = cpu_common_realizefn;
    dc->no_user = 1;
}

static const TypeInfo cpu_type_info = {
    .name = TYPE_CPU,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(CPUState),
    .abstract = true,
    .class_size = sizeof(CPUClass),
    .class_init = cpu_class_init,
};

static void cpu_register_types(void)
{
    type_register_static(&cpu_type_info);
}

type_init(cpu_register_types)
