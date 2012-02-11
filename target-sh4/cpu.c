/*
 * QEMU SuperH CPU
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
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
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "cpu.h"
#include "qemu-common.h"


/* CPUClass::reset() */
static void superh_cpu_reset(CPUState *s)
{
    SuperHCPU *cpu = SUPERH_CPU(s);
    SuperHCPUClass *scc = SUPERH_CPU_GET_CLASS(cpu);
    CPUSH4State *env = &cpu->env;

    scc->parent_reset(s);

    cpu_state_reset(env);
}

static void superh_cpu_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);
    SuperHCPUClass *scc = SUPERH_CPU_CLASS(oc);

    scc->parent_reset = cc->reset;
    cc->reset = superh_cpu_reset;
}

static const TypeInfo superh_cpu_type_info = {
    .name = TYPE_SUPERH_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(SuperHCPU),
    .abstract = false,
    .class_size = sizeof(SuperHCPUClass),
    .class_init = superh_cpu_class_init,
};

static void superh_cpu_register_types(void)
{
    type_register_static(&superh_cpu_type_info);
}

type_init(superh_cpu_register_types)
