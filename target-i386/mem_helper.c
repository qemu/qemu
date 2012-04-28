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

#include "cpu.h"
#include "dyngen-exec.h"
#include "helper.h"

#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"
#endif /* !defined(CONFIG_USER_ONLY) */

/* broken thread support */

static spinlock_t global_cpu_lock = SPIN_LOCK_UNLOCKED;

void helper_lock(void)
{
    spin_lock(&global_cpu_lock);
}

void helper_unlock(void)
{
    spin_unlock(&global_cpu_lock);
}

void helper_cmpxchg8b(target_ulong a0)
{
    uint64_t d;
    int eflags;

    eflags = helper_cc_compute_all(CC_OP);
    d = ldq(a0);
    if (d == (((uint64_t)EDX << 32) | (uint32_t)EAX)) {
        stq(a0, ((uint64_t)ECX << 32) | (uint32_t)EBX);
        eflags |= CC_Z;
    } else {
        /* always do the store */
        stq(a0, d);
        EDX = (uint32_t)(d >> 32);
        EAX = (uint32_t)d;
        eflags &= ~CC_Z;
    }
    CC_SRC = eflags;
}

#ifdef TARGET_X86_64
void helper_cmpxchg16b(target_ulong a0)
{
    uint64_t d0, d1;
    int eflags;

    if ((a0 & 0xf) != 0) {
        raise_exception(env, EXCP0D_GPF);
    }
    eflags = helper_cc_compute_all(CC_OP);
    d0 = ldq(a0);
    d1 = ldq(a0 + 8);
    if (d0 == EAX && d1 == EDX) {
        stq(a0, EBX);
        stq(a0 + 8, ECX);
        eflags |= CC_Z;
    } else {
        /* always do the store */
        stq(a0, d0);
        stq(a0 + 8, d1);
        EDX = d1;
        EAX = d0;
        eflags &= ~CC_Z;
    }
    CC_SRC = eflags;
}
#endif

void helper_boundw(target_ulong a0, int v)
{
    int low, high;

    low = ldsw(a0);
    high = ldsw(a0 + 2);
    v = (int16_t)v;
    if (v < low || v > high) {
        raise_exception(env, EXCP05_BOUND);
    }
}

void helper_boundl(target_ulong a0, int v)
{
    int low, high;

    low = ldl(a0);
    high = ldl(a0 + 4);
    if (v < low || v > high) {
        raise_exception(env, EXCP05_BOUND);
    }
}

#if !defined(CONFIG_USER_ONLY)

#define MMUSUFFIX _mmu

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

#endif

#if !defined(CONFIG_USER_ONLY)
/* try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
/* XXX: fix it to restore all registers */
void tlb_fill(CPUX86State *env1, target_ulong addr, int is_write, int mmu_idx,
              uintptr_t retaddr)
{
    TranslationBlock *tb;
    int ret;
    CPUX86State *saved_env;

    saved_env = env;
    env = env1;

    ret = cpu_x86_handle_mmu_fault(env, addr, is_write, mmu_idx);
    if (ret) {
        if (retaddr) {
            /* now we have a real cpu fault */
            tb = tb_find_pc(retaddr);
            if (tb) {
                /* the PC is inside the translated code. It means that we have
                   a virtual CPU fault */
                cpu_restore_state(tb, env, retaddr);
            }
        }
        raise_exception_err(env, env->exception_index, env->error_code);
    }
    env = saved_env;
}
#endif

/* temporary wrappers */
#if defined(CONFIG_USER_ONLY)
#define ldub_data(addr) ldub_raw(addr)
#define lduw_data(addr) lduw_raw(addr)
#define ldl_data(addr) ldl_raw(addr)
#define ldq_data(addr) ldq_raw(addr)

#define stb_data(addr, data) stb_raw(addr, data)
#define stw_data(addr, data) stw_raw(addr, data)
#define stl_data(addr, data) stl_raw(addr, data)
#define stq_data(addr, data) stq_raw(addr, data)
#endif

#define WRAP_LD(rettype, fn)                                    \
    rettype cpu_ ## fn(CPUX86State *env1, target_ulong addr)    \
    {                                                           \
        CPUX86State *saved_env;                                 \
        rettype ret;                                            \
                                                                \
        saved_env = env;                                        \
        env = env1;                                             \
        ret = fn(addr);                                         \
        env = saved_env;                                        \
        return ret;                                             \
    }

WRAP_LD(uint32_t, ldub_data)
WRAP_LD(uint32_t, lduw_data)
WRAP_LD(uint32_t, ldl_data)
WRAP_LD(uint64_t, ldq_data)
#undef WRAP_LD

#define WRAP_ST(datatype, fn)                                           \
    void cpu_ ## fn(CPUX86State *env1, target_ulong addr, datatype val) \
    {                                                                   \
        CPUX86State *saved_env;                                         \
                                                                        \
        saved_env = env;                                                \
        env = env1;                                                     \
        fn(addr, val);                                                  \
        env = saved_env;                                                \
    }

WRAP_ST(uint32_t, stb_data)
WRAP_ST(uint32_t, stw_data)
WRAP_ST(uint32_t, stl_data)
WRAP_ST(uint64_t, stq_data)
#undef WRAP_ST
