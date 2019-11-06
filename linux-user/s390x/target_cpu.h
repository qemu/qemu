/*
 * S/390 specific CPU ABI and functions for linux-user
 *
 * Copyright (c) 2009 Ulrich Hecht
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef S390X_TARGET_CPU_H
#define S390X_TARGET_CPU_H

static inline void cpu_clone_regs_child(CPUS390XState *env, target_ulong newsp,
                                        unsigned flags)
{
    if (newsp) {
        env->regs[15] = newsp;
    }
    env->regs[2] = 0;
}

static inline void cpu_clone_regs_parent(CPUS390XState *env, unsigned flags)
{
}

static inline void cpu_set_tls(CPUS390XState *env, target_ulong newtls)
{
    env->aregs[0] = newtls >> 32;
    env->aregs[1] = newtls & 0xffffffffULL;
}

static inline abi_ulong get_sp_from_cpustate(CPUS390XState *state)
{
   return state->regs[15];
}
#endif
