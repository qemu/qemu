/*
 * i386 TCG cpu class initialization
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
#include "cpu.h"
#include "tcg-cpu.h"
#include "exec/exec-all.h"
#include "sysemu/runstate.h"
#include "helper-tcg.h"

#if !defined(CONFIG_USER_ONLY)
#include "hw/i386/apic.h"
#endif

/* Frob eflags into and out of the CPU temporary format.  */

static void x86_cpu_exec_enter(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    CC_SRC = env->eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    env->df = 1 - (2 * ((env->eflags >> 10) & 1));
    CC_OP = CC_OP_EFLAGS;
    env->eflags &= ~(DF_MASK | CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
}

static void x86_cpu_exec_exit(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    env->eflags = cpu_compute_eflags(env);
}

static void x86_cpu_synchronize_from_tb(CPUState *cs,
                                        const TranslationBlock *tb)
{
    X86CPU *cpu = X86_CPU(cs);

    cpu->env.eip = tb->pc - tb->cs_base;
}

#include "hw/core/tcg-cpu-ops.h"

static struct TCGCPUOps x86_tcg_ops = {
    .initialize = tcg_x86_init,
    .synchronize_from_tb = x86_cpu_synchronize_from_tb,
    .cpu_exec_enter = x86_cpu_exec_enter,
    .cpu_exec_exit = x86_cpu_exec_exit,
    .cpu_exec_interrupt = x86_cpu_exec_interrupt,
    .do_interrupt = x86_cpu_do_interrupt,
    .tlb_fill = x86_cpu_tlb_fill,
#ifndef CONFIG_USER_ONLY
    .debug_excp_handler = breakpoint_handler,
#endif /* !CONFIG_USER_ONLY */
};

void tcg_cpu_common_class_init(CPUClass *cc)
{
    cc->tcg_ops = &x86_tcg_ops;
}
