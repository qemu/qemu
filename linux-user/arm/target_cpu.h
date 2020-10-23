/*
 * ARM specific CPU ABI and functions for linux-user
 *
 * Copyright (c) 2003 Fabrice Bellard
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
#ifndef ARM_TARGET_CPU_H
#define ARM_TARGET_CPU_H

static inline unsigned long arm_max_reserved_va(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);

    if (arm_feature(&cpu->env, ARM_FEATURE_M)) {
        /*
         * There are magic return addresses above 0xfe000000,
         * and in general a lot of M-profile system stuff in
         * the high addresses.  Restrict linux-user to the
         * cached write-back RAM in the system map.
         */
        return 0x80000000ul;
    } else {
        /*
         * We need to be able to map the commpage.
         * See validate_guest_space in linux-user/elfload.c.
         */
        return 0xffff0000ul;
    }
}
#define MAX_RESERVED_VA  arm_max_reserved_va

static inline void cpu_clone_regs_child(CPUARMState *env, target_ulong newsp,
                                        unsigned flags)
{
    if (newsp) {
        env->regs[13] = newsp;
    }
    env->regs[0] = 0;
}

static inline void cpu_clone_regs_parent(CPUARMState *env, unsigned flags)
{
}

static inline void cpu_set_tls(CPUARMState *env, target_ulong newtls)
{
    if (access_secure_reg(env)) {
        env->cp15.tpidruro_s = newtls;
    } else {
        env->cp15.tpidrro_el[0] = newtls;
    }
}

static inline target_ulong cpu_get_tls(CPUARMState *env)
{
    if (access_secure_reg(env)) {
        return env->cp15.tpidruro_s;
    } else {
        return env->cp15.tpidrro_el[0];
    }
}

static inline abi_ulong get_sp_from_cpustate(CPUARMState *state)
{
   return state->regs[13];
}
#endif
