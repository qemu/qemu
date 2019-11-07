/*
 * TILE-Gx specific CPU ABI and functions for linux-user
 *
 * Copyright (c) 2015 Chen Gang
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
#ifndef TILEGX_TARGET_CPU_H
#define TILEGX_TARGET_CPU_H

static inline void cpu_clone_regs_child(CPUTLGState *env, target_ulong newsp,
                                        unsigned flags)
{
    if (newsp) {
        env->regs[TILEGX_R_SP] = newsp;
    }
    env->regs[TILEGX_R_RE] = 0;
}

static inline void cpu_clone_regs_parent(CPUTLGState *env, unsigned flags)
{
}

static inline void cpu_set_tls(CPUTLGState *env, target_ulong newtls)
{
    env->regs[TILEGX_R_TP] = newtls;
}

static inline abi_ulong get_sp_from_cpustate(CPUTLGState *state)
{
    return state->regs[TILEGX_R_SP];
}
#endif
