/*
 *  PowerPC emulation definitions for qemu.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
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
#if !defined (__PPC_H__)
#define __PPC_H__

#include "config.h"

#include "dyngen-exec.h"

#include "cpu.h"
#include "exec-all.h"

register struct CPUPPCState *env asm(AREG0);

#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"
#endif /* !defined(CONFIG_USER_ONLY) */

static inline int cpu_has_work(CPUState *env)
{
    return (msr_ee && (env->interrupt_request & CPU_INTERRUPT_HARD));
}


static inline int cpu_halted(CPUState *env)
{
    if (!env->halted)
        return 0;
    if (cpu_has_work(env)) {
        env->halted = 0;
        return 0;
    }
    return EXCP_HALTED;
}

static inline void cpu_pc_from_tb(CPUState *env, TranslationBlock *tb)
{
    env->nip = tb->pc;
}

#endif /* !defined (__PPC_H__) */
