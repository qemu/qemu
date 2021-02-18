/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#ifndef HEXAGON_TARGET_CPU_H
#define HEXAGON_TARGET_CPU_H

static inline void cpu_clone_regs_child(CPUHexagonState *env,
                                        target_ulong newsp, unsigned flags)
{
    if (newsp) {
        env->gpr[HEX_REG_SP] = newsp;
    }
    env->gpr[0] = 0;
}

static inline void cpu_clone_regs_parent(CPUHexagonState *env, unsigned flags)
{
}

static inline void cpu_set_tls(CPUHexagonState *env, target_ulong newtls)
{
    env->gpr[HEX_REG_UGP] = newtls;
}

static inline abi_ulong get_sp_from_cpustate(CPUHexagonState *state)
{
    return state->gpr[HEX_REG_SP];
}

#endif
