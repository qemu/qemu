/*
 * HPPA specific CPU ABI and functions for linux-user
 *
 *  Copyright (c) 2016  Richard Henderson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef HPPA_TARGET_CPU_H
#define HPPA_TARGET_CPU_H

static inline void cpu_clone_regs_child(CPUHPPAState *env, target_ulong newsp,
                                        unsigned flags)
{
    if (newsp) {
        env->gr[30] = newsp;
    }
    /* Indicate child in return value.  */
    env->gr[28] = 0;
    /* Return from the syscall.  */
    env->iaoq_f = env->gr[31] | PRIV_USER;
    env->iaoq_b = env->iaoq_f + 4;
}

static inline void cpu_clone_regs_parent(CPUHPPAState *env, unsigned flags)
{
}

static inline void cpu_set_tls(CPUHPPAState *env, target_ulong newtls)
{
    env->cr[27] = newtls;
}

static inline abi_ulong get_sp_from_cpustate(CPUHPPAState *state)
{
    return state->gr[30];
}
#endif
