/*
 * SPARC specific CPU ABI and functions for linux-user
 *
 * Copyright (C) 2003 Thomas M. Ogrisegg <tom@fnord.at>
 * Copyright (C) 2003-2005 Fabrice Bellard
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

static inline void cpu_clone_regs(CPUSPARCState *env, target_ulong newsp)
{
    if (newsp) {
        env->regwptr[22] = newsp;
    }
    /* syscall return for clone child: 0, and clear CF since
     * this counts as a success return value.
     */
    env->regwptr[0] = 0;
#if defined(TARGET_SPARC64) && !defined(TARGET_ABI32)
    env->xcc &= ~PSR_CARRY;
#else
    env->psr &= ~PSR_CARRY;
#endif
}

static inline void cpu_set_tls(CPUSPARCState *env, target_ulong newtls)
{
    env->gregs[7] = newtls;
}

#endif
