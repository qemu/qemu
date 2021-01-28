/*
 * ARM AArch64 specific CPU ABI and functions for linux-user
 *
 * Copyright (c) 2013 Alexander Graf <agraf@suse.de>
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
#ifndef AARCH64_TARGET_CPU_H
#define AARCH64_TARGET_CPU_H

static inline void cpu_clone_regs_child(CPUARMState *env, target_ulong newsp,
                                        unsigned flags)
{
    if (newsp) {
        env->xregs[31] = newsp;
    }
    env->xregs[0] = 0;
}

static inline void cpu_clone_regs_parent(CPUARMState *env, unsigned flags)
{
}

static inline void cpu_set_tls(CPUARMState *env, target_ulong newtls)
{
    /* Note that AArch64 Linux keeps the TLS pointer in TPIDR; this is
     * different from AArch32 Linux, which uses TPIDRRO.
     */
    env->cp15.tpidr_el[0] = newtls;
}

static inline abi_ulong get_sp_from_cpustate(CPUARMState *state)
{
   return state->xregs[31];
}
#endif
