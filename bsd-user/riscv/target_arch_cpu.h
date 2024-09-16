/*
 *  RISC-V CPU init and loop
 *
 *  Copyright (c) 2019 Mark Corbin
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

#ifndef TARGET_ARCH_CPU_H
#define TARGET_ARCH_CPU_H

#include "target_arch.h"
#include "signal-common.h"

#define TARGET_DEFAULT_CPU_MODEL "max"

static inline void target_cpu_init(CPURISCVState *env,
        struct target_pt_regs *regs)
{
    int i;

    for (i = 1; i < 32; i++) {
        env->gpr[i] = regs->regs[i];
    }

    env->pc = regs->sepc;
}

#endif /* TARGET_ARCH_CPU_H */
