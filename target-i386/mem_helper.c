/*
 *  x86 memory access helpers
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

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"

/* broken thread support */

#if defined(CONFIG_USER_ONLY)
QemuMutex global_cpu_lock;

void helper_lock(void)
{
    qemu_mutex_lock(&global_cpu_lock);
}

void helper_unlock(void)
{
    qemu_mutex_unlock(&global_cpu_lock);
}

void helper_lock_init(void)
{
    qemu_mutex_init(&global_cpu_lock);
}
#else
void helper_lock(void)
{
}

void helper_unlock(void)
{
}

void helper_lock_init(void)
{
}
#endif

void helper_cmpxchg8b(CPUX86State *env, target_ulong a0)
{
    uint64_t d;
    int eflags;

    eflags = cpu_cc_compute_all(env, CC_OP);
    d = cpu_ldq_data_ra(env, a0, GETPC());
    if (d == (((uint64_t)env->regs[R_EDX] << 32) | (uint32_t)env->regs[R_EAX])) {
        cpu_stq_data_ra(env, a0, ((uint64_t)env->regs[R_ECX] << 32)
                                  | (uint32_t)env->regs[R_EBX], GETPC());
        eflags |= CC_Z;
    } else {
        /* always do the store */
        cpu_stq_data_ra(env, a0, d, GETPC());
        env->regs[R_EDX] = (uint32_t)(d >> 32);
        env->regs[R_EAX] = (uint32_t)d;
        eflags &= ~CC_Z;
    }
    CC_SRC = eflags;
}

#ifdef TARGET_X86_64
void helper_cmpxchg16b(CPUX86State *env, target_ulong a0)
{
    uint64_t d0, d1;
    int eflags;

    if ((a0 & 0xf) != 0) {
        raise_exception_ra(env, EXCP0D_GPF, GETPC());
    }
    eflags = cpu_cc_compute_all(env, CC_OP);
    d0 = cpu_ldq_data_ra(env, a0, GETPC());
    d1 = cpu_ldq_data_ra(env, a0 + 8, GETPC());
    if (d0 == env->regs[R_EAX] && d1 == env->regs[R_EDX]) {
        cpu_stq_data_ra(env, a0, env->regs[R_EBX], GETPC());
        cpu_stq_data_ra(env, a0 + 8, env->regs[R_ECX], GETPC());
        eflags |= CC_Z;
    } else {
        /* always do the store */
        cpu_stq_data_ra(env, a0, d0, GETPC());
        cpu_stq_data_ra(env, a0 + 8, d1, GETPC());
        env->regs[R_EDX] = d1;
        env->regs[R_EAX] = d0;
        eflags &= ~CC_Z;
    }
    CC_SRC = eflags;
}
#endif

void helper_boundw(CPUX86State *env, target_ulong a0, int v)
{
    int low, high;

    low = cpu_ldsw_data_ra(env, a0, GETPC());
    high = cpu_ldsw_data_ra(env, a0 + 2, GETPC());
    v = (int16_t)v;
    if (v < low || v > high) {
        if (env->hflags & HF_MPX_EN_MASK) {
            env->bndcs_regs.sts = 0;
        }
        raise_exception_ra(env, EXCP05_BOUND, GETPC());
    }
}

void helper_boundl(CPUX86State *env, target_ulong a0, int v)
{
    int low, high;

    low = cpu_ldl_data_ra(env, a0, GETPC());
    high = cpu_ldl_data_ra(env, a0 + 4, GETPC());
    if (v < low || v > high) {
        if (env->hflags & HF_MPX_EN_MASK) {
            env->bndcs_regs.sts = 0;
        }
        raise_exception_ra(env, EXCP05_BOUND, GETPC());
    }
}

#if !defined(CONFIG_USER_ONLY)
/* try to fill the TLB and return an exception if error. If retaddr is
 * NULL, it means that the function was called in C code (i.e. not
 * from generated code or from helper.c)
 */
/* XXX: fix it to restore all registers */
void tlb_fill(CPUState *cs, target_ulong addr, int is_write, int mmu_idx,
              uintptr_t retaddr)
{
    int ret;

    ret = x86_cpu_handle_mmu_fault(cs, addr, is_write, mmu_idx);
    if (ret) {
        X86CPU *cpu = X86_CPU(cs);
        CPUX86State *env = &cpu->env;

        raise_exception_err_ra(env, cs->exception_index, env->error_code, retaddr);
    }
}
#endif
