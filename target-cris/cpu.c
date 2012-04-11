/*
 * QEMU CRIS CPU
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
static void cris_cpu_reset(CPUState *s)
{
    CRISCPU *cpu = CRIS_CPU(s);
    CRISCPUClass *ccc = CRIS_CPU_GET_CLASS(cpu);
    CPUCRISState *env = &cpu->env;

    ccc->parent_reset(s);

    cpu_state_reset(env);
}

static void cris_cpu_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);
    CRISCPUClass *ccc = CRIS_CPU_CLASS(oc);

    ccc->parent_reset = cc->reset;
    cc->reset = cris_cpu_reset;
}

static const TypeInfo cris_cpu_type_info = {
    .name = TYPE_CRIS_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(CRISCPU),
    .abstract = false,
    .class_size = sizeof(CRISCPUClass),
    .class_init = cris_cpu_class_init,
};

static void cris_cpu_register_types(void)
{
    type_register_static(&cris_cpu_type_info);
}

type_init(cris_cpu_register_types)
