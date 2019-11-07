/*
 * i386 specific CPU ABI and functions for linux-user
 *
 *  Copyright (c) 2003 Fabrice Bellard
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

#ifndef I386_TARGET_CPU_H
#define I386_TARGET_CPU_H

static inline void cpu_clone_regs_child(CPUX86State *env, target_ulong newsp,
                                        unsigned flags)
{
    if (newsp) {
        env->regs[R_ESP] = newsp;
    }
    env->regs[R_EAX] = 0;
}

static inline void cpu_clone_regs_parent(CPUX86State *env, unsigned flags)
{
}

#if defined(TARGET_ABI32)
abi_long do_set_thread_area(CPUX86State *env, abi_ulong ptr);

static inline void cpu_set_tls(CPUX86State *env, target_ulong newtls)
{
    do_set_thread_area(env, newtls);
    cpu_x86_load_seg(env, R_GS, env->segs[R_GS].selector);
}
#else
abi_long do_arch_prctl(CPUX86State *env, int code, abi_ulong addr);

static inline void cpu_set_tls(CPUX86State *env, target_ulong newtls)
{
    do_arch_prctl(env, TARGET_ARCH_SET_FS, newtls);
}
#endif /* defined(TARGET_ABI32) */

static inline abi_ulong get_sp_from_cpustate(CPUX86State *state)
{
    return state->regs[R_ESP];
}
#endif /* I386_TARGET_CPU_H */
