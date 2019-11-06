/*
 * CRIS specific CPU ABI and functions for linux-user
 *
 * Copyright (c) 2007 AXIS Communications AB
 * Written by Edgar E. Iglesias
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
#ifndef CRIS_TARGET_CPU_H
#define CRIS_TARGET_CPU_H

static inline void cpu_clone_regs_child(CPUCRISState *env, target_ulong newsp,
                                        unsigned flags)
{
    if (newsp) {
        env->regs[14] = newsp;
    }
    env->regs[10] = 0;
}

static inline void cpu_clone_regs_parent(CPUCRISState *env, unsigned flags)
{
}

static inline void cpu_set_tls(CPUCRISState *env, target_ulong newtls)
{
    env->pregs[PR_PID] = (env->pregs[PR_PID] & 0xff) | newtls;
}

static inline abi_ulong get_sp_from_cpustate(CPUCRISState *state)
{
    return state->regs[14];
}
#endif
