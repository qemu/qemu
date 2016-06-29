/*
 * SH4 specific CPU ABI and functions for linux-user
 *
 * Copyright (c) 2005 Samuel Tardieu
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
#ifndef SH4_TARGET_CPU_H
#define SH4_TARGET_CPU_H

static inline void cpu_clone_regs(CPUSH4State *env, target_ulong newsp)
{
    if (newsp) {
        env->gregs[15] = newsp;
    }
    env->gregs[0] = 0;
}

static inline void cpu_set_tls(CPUSH4State *env, target_ulong newtls)
{
  env->gbr = newtls;
}

#endif
