/*
 * QEMU Motorola 68k CPU
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
static void m68k_cpu_reset(CPUState *s)
{
    M68kCPU *cpu = M68K_CPU(s);
    M68kCPUClass *mcc = M68K_CPU_GET_CLASS(cpu);
    CPUM68KState *env = &cpu->env;

    if (qemu_loglevel_mask(CPU_LOG_RESET)) {
        qemu_log("CPU Reset (CPU %d)\n", env->cpu_index);
        log_cpu_state(env, 0);
    }

    mcc->parent_reset(s);

    memset(env, 0, offsetof(CPUM68KState, breakpoints));
#if !defined(CONFIG_USER_ONLY)
    env->sr = 0x2700;
#endif
    m68k_switch_sp(env);
    /* ??? FP regs should be initialized to NaN.  */
    env->cc_op = CC_OP_FLAGS;
    /* TODO: We should set PC from the interrupt vector.  */
    env->pc = 0;
    tlb_flush(env, 1);
}

static void m68k_cpu_class_init(ObjectClass *c, void *data)
{
    M68kCPUClass *mcc = M68K_CPU_CLASS(c);
    CPUClass *cc = CPU_CLASS(c);

    mcc->parent_reset = cc->reset;
    cc->reset = m68k_cpu_reset;
}

static const TypeInfo m68k_cpu_type_info = {
    .name = TYPE_M68K_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(M68kCPU),
    .abstract = false,
    .class_size = sizeof(M68kCPUClass),
    .class_init = m68k_cpu_class_init,
};

static void m68k_cpu_register_types(void)
{
    type_register_static(&m68k_cpu_type_info);
}

type_init(m68k_cpu_register_types)
