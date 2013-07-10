/*
 * Alpha specific CPU ABI and functions for linux-user
 *
 *  Copyright (c) 2007 Jocelyn Mayer
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

static inline void cpu_clone_regs(CPUAlphaState *env, target_ulong newsp)
{
    if (newsp) {
        env->ir[IR_SP] = newsp;
    }
    env->ir[IR_V0] = 0;
    env->ir[IR_A3] = 0;
}

static inline void cpu_set_tls(CPUAlphaState *env, target_ulong newtls)
{
    env->unique = newtls;
}

#endif
