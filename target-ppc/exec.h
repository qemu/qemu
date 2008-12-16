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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#if !defined (__PPC_H__)
#define __PPC_H__

#include "config.h"

#include "dyngen-exec.h"

#include "cpu.h"
#include "exec-all.h"

/* Precise emulation is needed to correctly emulation exception flags */
#define USE_PRECISE_EMULATION 1

register struct CPUPPCState *env asm(AREG0);

#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"
#endif /* !defined(CONFIG_USER_ONLY) */

static always_inline void env_to_regs (void)
{
}

static always_inline void regs_to_env (void)
{
}

static always_inline int cpu_halted (CPUState *env)
{
    if (!env->halted)
        return 0;
    if (msr_ee && (env->interrupt_request & CPU_INTERRUPT_HARD)) {
        env->halted = 0;
        return 0;
    }
    return EXCP_HALTED;
}

#endif /* !defined (__PPC_H__) */
