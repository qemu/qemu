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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#if !defined (__ALPHA_EXEC_H__)
#define __ALPHA_EXEC_H__

#include "config.h"

#include "dyngen-exec.h"

#define TARGET_LONG_BITS 64

register struct CPUAlphaState *env asm(AREG0);

#if TARGET_LONG_BITS > HOST_LONG_BITS

/* no registers can be used */
#define T0 (env->t0)
#define T1 (env->t1)

#else

register uint64_t T0 asm(AREG1);
register uint64_t T1 asm(AREG2);

#endif /* TARGET_LONG_BITS > HOST_LONG_BITS */

#define PARAM(n) ((uint64_t)PARAM##n)
#define SPARAM(n) ((int32_t)PARAM##n)
#define FP_STATUS (env->fp_status)

#if defined (DEBUG_OP)
#define RETURN() __asm__ __volatile__("nop" : : : "memory");
#else
#define RETURN() __asm__ __volatile__("" : : : "memory");
#endif

#include "cpu.h"
#include "exec-all.h"

#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"
#endif /* !defined(CONFIG_USER_ONLY) */

static always_inline void env_to_regs(void)
{
}

static always_inline void regs_to_env(void)
{
}

int cpu_alpha_handle_mmu_fault (CPUState *env, uint64_t address, int rw,
                                int mmu_idx, int is_softmmu);

void do_interrupt (CPUState *env);

static always_inline int cpu_halted(CPUState *env) {
    if (!env->halted)
        return 0;
    if (env->interrupt_request & CPU_INTERRUPT_HARD) {
        env->halted = 0;
        return 0;
    }
    return EXCP_HALTED;
}

#endif /* !defined (__ALPHA_EXEC_H__) */
