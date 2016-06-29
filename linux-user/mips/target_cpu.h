/*
 * MIPS specific CPU ABI and functions for linux-user
 *
 * Copyright (c) 2004-2005 Jocelyn Mayer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef MIPS_TARGET_CPU_H
#define MIPS_TARGET_CPU_H

static inline void cpu_clone_regs(CPUMIPSState *env, target_ulong newsp)
{
    if (newsp) {
        env->active_tc.gpr[29] = newsp;
    }
    env->active_tc.gpr[7] = 0;
    env->active_tc.gpr[2] = 0;
}

static inline void cpu_set_tls(CPUMIPSState *env, target_ulong newtls)
{
    env->active_tc.CP0_UserLocal = newtls;
}

#endif
