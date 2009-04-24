/*
 *  m68k execution defines
 *
 *  Copyright (c) 2005-2006 CodeSourcery
 *  Written by Paul Brook
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */
#include "dyngen-exec.h"

register struct CPUM68KState *env asm(AREG0);
/* This is only used for tb lookup.  */
register uint32_t T0 asm(AREG1);
/* ??? We don't use T1, but common code expects it to exist  */
#define T1 env->t1

#include "cpu.h"
#include "exec-all.h"

static inline void env_to_regs(void)
{
}

static inline void regs_to_env(void)
{
}

#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"
#endif

static inline int cpu_has_work(CPUState *env)
{
    return (env->interrupt_request & (CPU_INTERRUPT_HARD));
}

static inline int cpu_halted(CPUState *env) {
    if (!env->halted)
        return 0;
    if (cpu_has_work(env)) {
        env->halted = 0;
        return 0;
    }
    return EXCP_HALTED;
}
