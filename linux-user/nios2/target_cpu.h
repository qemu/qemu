/*
 * Nios2 specific CPU ABI and functions for linux-user
 *
 * Copyright (c) 2016 Marek Vasut <marex@denx.de>
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

#ifndef TARGET_CPU_H
#define TARGET_CPU_H

static inline void cpu_clone_regs(CPUNios2State *env, target_ulong newsp)
{
    if (newsp) {
        env->regs[R_SP] = newsp;
    }
    env->regs[R_RET0] = 0;
}

static inline void cpu_set_tls(CPUNios2State *env, target_ulong newtls)
{
    /*
     * Linux kernel 3.10 does not pay any attention to CLONE_SETTLS
     * in copy_thread(), so QEMU need not do so either.
     */
}

#endif
