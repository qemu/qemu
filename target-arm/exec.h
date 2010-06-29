/*
 *  ARM execution defines
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
#include "config.h"
#include "dyngen-exec.h"

register struct CPUARMState *env asm(AREG0);

#define M0   env->iwmmxt.val

#include "cpu.h"
#include "exec-all.h"

static inline int cpu_has_work(CPUState *env)
{
    return (env->interrupt_request &
            (CPU_INTERRUPT_FIQ | CPU_INTERRUPT_HARD | CPU_INTERRUPT_EXITTB));
}

static inline int cpu_halted(CPUState *env) {
    if (!env->halted)
        return 0;
    /* An interrupt wakes the CPU even if the I and F CPSR bits are
       set.  We use EXITTB to silently wake CPU without causing an
       actual interrupt.  */
    if (cpu_has_work(env)) {
        env->halted = 0;
        return 0;
    }
    return EXCP_HALTED;
}

#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"
#endif

void raise_exception(int);

static inline void cpu_pc_from_tb(CPUState *env, TranslationBlock *tb)
{
    env->regs[15] = tb->pc;
}

