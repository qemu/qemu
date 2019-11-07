/*
 * OpenRISC specific CPU ABI and functions for linux-user
 *
 * Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
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

#ifndef OPENRISC_TARGET_CPU_H
#define OPENRISC_TARGET_CPU_H

static inline void cpu_clone_regs_child(CPUOpenRISCState *env,
                                        target_ulong newsp,
                                        unsigned flags)
{
    if (newsp) {
        cpu_set_gpr(env, 1, newsp);
    }
    cpu_set_gpr(env, 11, 0);
}

static inline void cpu_clone_regs_parent(CPUOpenRISCState *env, unsigned flags)
{
}

static inline void cpu_set_tls(CPUOpenRISCState *env, target_ulong newtls)
{
    cpu_set_gpr(env, 10, newtls);
}

static inline abi_ulong get_sp_from_cpustate(CPUOpenRISCState *state)
{
    return cpu_get_gpr(state, 1);
}
#endif
