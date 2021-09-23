/*
 *  arm cpu init and loop
 *
 *  Copyright (c) 2013 Stacey D. Son
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _TARGET_ARCH_CPU_H_
#define _TARGET_ARCH_CPU_H_

#include "target_arch.h"

#define TARGET_DEFAULT_CPU_MODEL "any"

static inline void target_cpu_init(CPUARMState *env,
        struct target_pt_regs *regs)
{
    int i;

    cpsr_write(env, regs->uregs[16], CPSR_USER | CPSR_EXEC,
               CPSRWriteByInstr);
    for (i = 0; i < 16; i++) {
        env->regs[i] = regs->uregs[i];
    }
}

static inline void target_cpu_clone_regs(CPUARMState *env, target_ulong newsp)
{
    if (newsp) {
        env->regs[13] = newsp;
    }
    env->regs[0] = 0;
}

static inline void target_cpu_reset(CPUArchState *cpu)
{
}

#endif /* !_TARGET_ARCH_CPU_H */
