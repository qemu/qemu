/*
 *  ARM helper routines
 *
 *  Copyright (c) 2005-2007 CodeSourcery, LLC
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "exec.h"

void raise_exception(int tt)
{
    env->exception_index = tt;
    cpu_loop_exit();
}

/* thread support */

spinlock_t global_cpu_lock = SPIN_LOCK_UNLOCKED;

void cpu_lock(void)
{
    spin_lock(&global_cpu_lock);
}

void cpu_unlock(void)
{
    spin_unlock(&global_cpu_lock);
}

/* VFP support.  */

void do_vfp_abss(void)
{
    FT0s = float32_abs(FT0s);
}

void do_vfp_absd(void)
{
    FT0d = float64_abs(FT0d);
}

void do_vfp_sqrts(void)
{
    FT0s = float32_sqrt(FT0s, &env->vfp.fp_status);
}

void do_vfp_sqrtd(void)
{
    FT0d = float64_sqrt(FT0d, &env->vfp.fp_status);
}

/* XXX: check quiet/signaling case */
#define DO_VFP_cmp(p, size)               \
void do_vfp_cmp##p(void)                  \
{                                         \
    uint32_t flags;                       \
    switch(float ## size ## _compare_quiet(FT0##p, FT1##p, &env->vfp.fp_status)) {\
    case 0: flags = 0x6; break;\
    case -1: flags = 0x8; break;\
    case 1: flags = 0x2; break;\
    default: case 2: flags = 0x3; break;\
    }\
    env->vfp.xregs[ARM_VFP_FPSCR] = (flags << 28)\
        | (env->vfp.xregs[ARM_VFP_FPSCR] & 0x0fffffff); \
    FORCE_RET();                          \
}\
\
void do_vfp_cmpe##p(void)                   \
{                                           \
    uint32_t flags;                       \
    switch(float ## size ## _compare(FT0##p, FT1##p, &env->vfp.fp_status)) {\
    case 0: flags = 0x6; break;\
    case -1: flags = 0x8; break;\
    case 1: flags = 0x2; break;\
    default: case 2: flags = 0x3; break;\
    }\
    env->vfp.xregs[ARM_VFP_FPSCR] = (flags << 28)\
        | (env->vfp.xregs[ARM_VFP_FPSCR] & 0x0fffffff); \
    FORCE_RET();                          \
}
DO_VFP_cmp(s, 32)
DO_VFP_cmp(d, 64)
#undef DO_VFP_cmp

/* Convert host exception flags to vfp form.  */
static inline int vfp_exceptbits_from_host(int host_bits)
{
    int target_bits = 0;

    if (host_bits & float_flag_invalid)
        target_bits |= 1;
    if (host_bits & float_flag_divbyzero)
        target_bits |= 2;
    if (host_bits & float_flag_overflow)
        target_bits |= 4;
    if (host_bits & float_flag_underflow)
        target_bits |= 8;
    if (host_bits & float_flag_inexact)
        target_bits |= 0x10;
    return target_bits;
}

/* Convert vfp exception flags to target form.  */
static inline int vfp_exceptbits_to_host(int target_bits)
{
    int host_bits = 0;

    if (target_bits & 1)
        host_bits |= float_flag_invalid;
    if (target_bits & 2)
        host_bits |= float_flag_divbyzero;
    if (target_bits & 4)
        host_bits |= float_flag_overflow;
    if (target_bits & 8)
        host_bits |= float_flag_underflow;
    if (target_bits & 0x10)
        host_bits |= float_flag_inexact;
    return host_bits;
}

void do_vfp_set_fpscr(void)
{
    int i;
    uint32_t changed;

    changed = env->vfp.xregs[ARM_VFP_FPSCR];
    env->vfp.xregs[ARM_VFP_FPSCR] = (T0 & 0xffc8ffff);
    env->vfp.vec_len = (T0 >> 16) & 7;
    env->vfp.vec_stride = (T0 >> 20) & 3;

    changed ^= T0;
    if (changed & (3 << 22)) {
        i = (T0 >> 22) & 3;
        switch (i) {
        case 0:
            i = float_round_nearest_even;
            break;
        case 1:
            i = float_round_up;
            break;
        case 2:
            i = float_round_down;
            break;
        case 3:
            i = float_round_to_zero;
            break;
        }
        set_float_rounding_mode(i, &env->vfp.fp_status);
    }

    i = vfp_exceptbits_to_host((T0 >> 8) & 0x1f);
    set_float_exception_flags(i, &env->vfp.fp_status);
    /* XXX: FZ and DN are not implemented.  */
}

void do_vfp_get_fpscr(void)
{
    int i;

    T0 = (env->vfp.xregs[ARM_VFP_FPSCR] & 0xffc8ffff) | (env->vfp.vec_len << 16)
          | (env->vfp.vec_stride << 20);
    i = get_float_exception_flags(&env->vfp.fp_status);
    T0 |= vfp_exceptbits_from_host(i);
}

float32 helper_recps_f32(float32 a, float32 b)
{
    float_status *s = &env->vfp.fp_status;
    float32 two = int32_to_float32(2, s);
    return float32_sub(two, float32_mul(a, b, s), s);
}

float32 helper_rsqrts_f32(float32 a, float32 b)
{
    float_status *s = &env->vfp.fp_status;
    float32 three = int32_to_float32(3, s);
    return float32_sub(three, float32_mul(a, b, s), s);
}

/* TODO: The architecture specifies the value that the estimate functions
   should return.  We return the exact reciprocal/root instead.  */
float32 helper_recpe_f32(float32 a)
{
    float_status *s = &env->vfp.fp_status;
    float32 one = int32_to_float32(1, s);
    return float32_div(one, a, s);
}

float32 helper_rsqrte_f32(float32 a)
{
    float_status *s = &env->vfp.fp_status;
    float32 one = int32_to_float32(1, s);
    return float32_div(one, float32_sqrt(a, s), s);
}

uint32_t helper_recpe_u32(uint32_t a)
{
    float_status *s = &env->vfp.fp_status;
    float32 tmp;
    tmp = int32_to_float32(a, s);
    tmp = float32_scalbn(tmp, -32, s);
    tmp = helper_recpe_f32(tmp);
    tmp = float32_scalbn(tmp, 31, s);
    return float32_to_int32(tmp, s);
}

uint32_t helper_rsqrte_u32(uint32_t a)
{
    float_status *s = &env->vfp.fp_status;
    float32 tmp;
    tmp = int32_to_float32(a, s);
    tmp = float32_scalbn(tmp, -32, s);
    tmp = helper_rsqrte_f32(tmp);
    tmp = float32_scalbn(tmp, 31, s);
    return float32_to_int32(tmp, s);
}

void helper_neon_tbl(int rn, int maxindex)
{
    uint32_t val;
    uint32_t mask;
    uint32_t tmp;
    int index;
    int shift;
    uint64_t *table;
    table = (uint64_t *)&env->vfp.regs[rn];
    val = 0;
    mask = 0;
    for (shift = 0; shift < 32; shift += 8) {
        index = (T1 >> shift) & 0xff;
        if (index <= maxindex) {
            tmp = (table[index >> 3] >> (index & 7)) & 0xff;
            val |= tmp << shift;
        } else {
            val |= T0 & (0xff << shift);
        }
    }
    T0 = val;
}

#if !defined(CONFIG_USER_ONLY)

#define MMUSUFFIX _mmu
#ifdef __s390__
# define GETPC() ((void*)((unsigned long)__builtin_return_address(0) & 0x7fffffffUL))
#else
# define GETPC() (__builtin_return_address(0))
#endif

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

/* try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
/* XXX: fix it to restore all registers */
void tlb_fill (target_ulong addr, int is_write, int mmu_idx, void *retaddr)
{
    TranslationBlock *tb;
    CPUState *saved_env;
    unsigned long pc;
    int ret;

    /* XXX: hack to restore env in all cases, even if not called from
       generated code */
    saved_env = env;
    env = cpu_single_env;
    ret = cpu_arm_handle_mmu_fault(env, addr, is_write, mmu_idx, 1);
    if (__builtin_expect(ret, 0)) {
        if (retaddr) {
            /* now we have a real cpu fault */
            pc = (unsigned long)retaddr;
            tb = tb_find_pc(pc);
            if (tb) {
                /* the PC is inside the translated code. It means that we have
                   a virtual CPU fault */
                cpu_restore_state(tb, env, pc, NULL);
            }
        }
        raise_exception(env->exception_index);
    }
    env = saved_env;
}
#endif
