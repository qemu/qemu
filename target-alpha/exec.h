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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */

#if !defined (__ALPHA_EXEC_H__)
#define __ALPHA_EXEC_H__

#include "config.h"

#include "dyngen-exec.h"

#define TARGET_LONG_BITS 64

register struct CPUAlphaState *env asm(AREG0);

#define PARAM(n) ((uint64_t)PARAM##n)
#define SPARAM(n) ((int32_t)PARAM##n)
#define FP_STATUS (env->fp_status)

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

static always_inline int cpu_has_work(CPUState *env)
{
    return (env->interrupt_request & CPU_INTERRUPT_HARD);
}

static always_inline int cpu_halted(CPUState *env) {
    if (!env->halted)
        return 0;
    if (cpu_has_work(env)) {
        env->halted = 0;
        return 0;
    }
    return EXCP_HALTED;
}

#endif /* !defined (__ALPHA_EXEC_H__) */
