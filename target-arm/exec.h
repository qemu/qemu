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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */
#include "config.h"
#include "dyngen-exec.h"

register struct CPUARMState *env asm(AREG0);
register uint32_t T0 asm(AREG1);
register uint32_t T1 asm(AREG2);

#define M0   env->iwmmxt.val

#include "cpu.h"
#include "exec-all.h"

static inline void env_to_regs(void)
{
}

static inline void regs_to_env(void)
{
}

int cpu_arm_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                              int mmu_idx, int is_softmmu);

static inline int cpu_halted(CPUState *env) {
    if (!env->halted)
        return 0;
    /* An interrupt wakes the CPU even if the I and F CPSR bits are
       set.  We use EXITTB to silently wake CPU without causing an
       actual interrupt.  */
    if (env->interrupt_request &
        (CPU_INTERRUPT_FIQ | CPU_INTERRUPT_HARD | CPU_INTERRUPT_EXITTB)) {
        env->halted = 0;
        return 0;
    }
    return EXCP_HALTED;
}

#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"
#endif

void raise_exception(int);
