/*
 * S/390 specific CPU ABI and functions for linux-user
 *
 * Copyright (c) 2009 Ulrich Hecht
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
 * Contributions after 2012-10-29 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 *
 * You should have received a copy of the GNU (Lesser) General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef TARGET_CPU_H
#define TARGET_CPU_H

static inline void cpu_clone_regs(CPUS390XState *env, target_ulong newsp)
{
    if (newsp) {
        env->regs[15] = newsp;
    }
    env->regs[2] = 0;
}

static inline void cpu_set_tls(CPUS390XState *env, target_ulong newtls)
{
    env->aregs[0] = newtls >> 32;
    env->aregs[1] = newtls & 0xffffffffULL;
}

#endif
