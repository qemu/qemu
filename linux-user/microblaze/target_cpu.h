/*
 * MicroBlaze specific CPU ABI and functions for linux-user
 *
 * Copyright (c) 2009 Edgar E. Iglesias
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef TARGET_CPU_H
#define TARGET_CPU_H

static inline void cpu_clone_regs(CPUMBState *env, target_ulong newsp)
{
    if (newsp) {
        env->regs[R_SP] = newsp;
    }
    env->regs[3] = 0;
}

static inline void cpu_set_tls(CPUMBState *env, target_ulong newtls)
{
    env->regs[21] = newtls;
}

#endif
