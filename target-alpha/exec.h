/*
 *  Alpha emulation cpu run-time definitions for qemu.
 *
 *  Copyright (c) 2007 Jocelyn Mayer
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

#if !defined (__ALPHA_EXEC_H__)
#define __ALPHA_EXEC_H__

#include "config.h"

#include "dyngen-exec.h"

#define TARGET_LONG_BITS 64

register struct CPUAlphaState *env asm(AREG0);

#define FP_STATUS (env->fp_status)

#include "cpu.h"
#include "exec-all.h"

#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"
#endif /* !defined(CONFIG_USER_ONLY) */

static inline bool cpu_has_work(CPUState *env)
{
    /* Here we are checking to see if the CPU should wake up from HALT.
       We will have gotten into this state only for WTINT from PALmode.  */
    /* ??? I'm not sure how the IPL state works with WTINT to keep a CPU
       asleep even if (some) interrupts have been asserted.  For now, 
       assume that if a CPU really wants to stay asleep, it will mask
       interrupts at the chipset level, which will prevent these bits
       from being set in the first place.  */
    return env->interrupt_request & (CPU_INTERRUPT_HARD
                                     | CPU_INTERRUPT_TIMER
                                     | CPU_INTERRUPT_SMP
                                     | CPU_INTERRUPT_MCHK);
}

static inline void cpu_pc_from_tb(CPUState *env, TranslationBlock *tb)
{
    env->pc = tb->pc;
}

#endif /* !defined (__ALPHA_EXEC_H__) */
