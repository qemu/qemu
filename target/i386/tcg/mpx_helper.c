/*
 *  x86 MPX helpers
 *
 *  Copyright (c) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#include "accel/tcg/cpu-ldst.h"
#include "exec/target_page.h"
#include "helper-tcg.h"


void helper_bndck(CPUX86State *env, uint32_t fail)
{
    if (unlikely(fail)) {
        env->bndcs_regs.sts = 1;
        raise_exception_ra(env, EXCP05_BOUND, GETPC());
    }
}

static uint64_t lookup_bte64(CPUX86State *env, uint64_t base, uintptr_t ra)
{
    uint64_t bndcsr, bde, bt;

    if ((env->hflags & HF_CPL_MASK) == 3) {
        bndcsr = env->bndcs_regs.cfgu;
    } else {
        bndcsr = env->msr_bndcfgs;
    }

    bde = (extract64(base, 20, 28) << 3) + (extract64(bndcsr, 20, 44) << 12);
    bt = cpu_ldq_data_ra(env, bde, ra);
    if ((bt & 1) == 0) {
        env->bndcs_regs.sts = bde | 2;
        raise_exception_ra(env, EXCP05_BOUND, ra);
    }

    return (extract64(base, 3, 17) << 5) + (bt & ~7);
}

static uint32_t lookup_bte32(CPUX86State *env, uint32_t base, uintptr_t ra)
{
    uint32_t bndcsr, bde, bt;

    if ((env->hflags & HF_CPL_MASK) == 3) {
        bndcsr = env->bndcs_regs.cfgu;
    } else {
        bndcsr = env->msr_bndcfgs;
    }

    bde = (extract32(base, 12, 20) << 2) + (bndcsr & TARGET_PAGE_MASK);
    bt = cpu_ldl_data_ra(env, bde, ra);
    if ((bt & 1) == 0) {
        env->bndcs_regs.sts = bde | 2;
        raise_exception_ra(env, EXCP05_BOUND, ra);
    }

    return (extract32(base, 2, 10) << 4) + (bt & ~3);
}

uint64_t helper_bndldx64(CPUX86State *env, target_ulong base, target_ulong ptr)
{
    uintptr_t ra = GETPC();
    uint64_t bte, lb, ub, pt;

    bte = lookup_bte64(env, base, ra);
    lb = cpu_ldq_data_ra(env, bte, ra);
    ub = cpu_ldq_data_ra(env, bte + 8, ra);
    pt = cpu_ldq_data_ra(env, bte + 16, ra);

    if (pt != ptr) {
        lb = ub = 0;
    }
    env->mmx_t0.MMX_Q(0) = ub;
    return lb;
}

uint64_t helper_bndldx32(CPUX86State *env, target_ulong base, target_ulong ptr)
{
    uintptr_t ra = GETPC();
    uint32_t bte, lb, ub, pt;

    bte = lookup_bte32(env, base, ra);
    lb = cpu_ldl_data_ra(env, bte, ra);
    ub = cpu_ldl_data_ra(env, bte + 4, ra);
    pt = cpu_ldl_data_ra(env, bte + 8, ra);

    if (pt != ptr) {
        lb = ub = 0;
    }
    return ((uint64_t)ub << 32) | lb;
}

void helper_bndstx64(CPUX86State *env, target_ulong base, target_ulong ptr,
                     uint64_t lb, uint64_t ub)
{
    uintptr_t ra = GETPC();
    uint64_t bte;

    bte = lookup_bte64(env, base, ra);
    cpu_stq_data_ra(env, bte, lb, ra);
    cpu_stq_data_ra(env, bte + 8, ub, ra);
    cpu_stq_data_ra(env, bte + 16, ptr, ra);
}

void helper_bndstx32(CPUX86State *env, target_ulong base, target_ulong ptr,
                     uint64_t lb, uint64_t ub)
{
    uintptr_t ra = GETPC();
    uint32_t bte;

    bte = lookup_bte32(env, base, ra);
    cpu_stl_data_ra(env, bte, lb, ra);
    cpu_stl_data_ra(env, bte + 4, ub, ra);
    cpu_stl_data_ra(env, bte + 8, ptr, ra);
}

void helper_bnd_jmp(CPUX86State *env)
{
    if (!(env->hflags2 & HF2_MPX_PR_MASK)) {
        memset(env->bnd_regs, 0, sizeof(env->bnd_regs));
        env->hflags &= ~HF_MPX_IU_MASK;
    }
}
