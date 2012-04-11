/*
 * QEMU CRIS CPU
 *
 * Copyright (c) 2008 AXIS Communications AB
 * Written by Edgar E. Iglesias.
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
#include "mmu.h"


/* CPUClass::reset() */
static void cris_cpu_reset(CPUState *s)
{
    CRISCPU *cpu = CRIS_CPU(s);
    CRISCPUClass *ccc = CRIS_CPU_GET_CLASS(cpu);
    CPUCRISState *env = &cpu->env;
    uint32_t vr;

    if (qemu_loglevel_mask(CPU_LOG_RESET)) {
        qemu_log("CPU Reset (CPU %d)\n", env->cpu_index);
        log_cpu_state(env, 0);
    }

    ccc->parent_reset(s);

    vr = env->pregs[PR_VR];
    memset(env, 0, offsetof(CPUCRISState, breakpoints));
    env->pregs[PR_VR] = vr;
    tlb_flush(env, 1);

#if defined(CONFIG_USER_ONLY)
    /* start in user mode with interrupts enabled.  */
    env->pregs[PR_CCS] |= U_FLAG | I_FLAG | P_FLAG;
#else
    cris_mmu_init(env);
    env->pregs[PR_CCS] = 0;
#endif
}

static void cris_cpu_initfn(Object *obj)
{
    CRISCPU *cpu = CRIS_CPU(obj);
    CPUCRISState *env = &cpu->env;

    cpu_exec_init(env);
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
    .instance_init = cris_cpu_initfn,
    .abstract = false,
    .class_size = sizeof(CRISCPUClass),
    .class_init = cris_cpu_class_init,
};

static void cris_cpu_register_types(void)
{
    type_register_static(&cris_cpu_type_info);
}

type_init(cris_cpu_register_types)
