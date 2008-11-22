/*
 *  Alpha emulation cpu micro-operations helpers for qemu.
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "exec.h"
#include "host-utils.h"
#include "softfloat.h"
#include "helper.h"

void helper_tb_flush (void)
{
    tlb_flush(env, 1);
}

/*****************************************************************************/
/* Exceptions processing helpers */
void helper_excp (int excp, int error)
{
    env->exception_index = excp;
    env->error_code = error;
    cpu_loop_exit();
}

uint64_t helper_amask (uint64_t arg)
{
    switch (env->implver) {
    case IMPLVER_2106x:
        /* EV4, EV45, LCA, LCA45 & EV5 */
        break;
    case IMPLVER_21164:
    case IMPLVER_21264:
    case IMPLVER_21364:
        arg &= ~env->amask;
        break;
    }
    return arg;
}

uint64_t helper_load_pcc (void)
{
    /* XXX: TODO */
    return 0;
}

uint64_t helper_load_implver (void)
{
    return env->implver;
}

uint64_t helper_load_fpcr (void)
{
    uint64_t ret = 0;
#ifdef CONFIG_SOFTFLOAT
    ret |= env->fp_status.float_exception_flags << 52;
    if (env->fp_status.float_exception_flags)
        ret |= 1ULL << 63;
    env->ipr[IPR_EXC_SUM] &= ~0x3E:
    env->ipr[IPR_EXC_SUM] |= env->fp_status.float_exception_flags << 1;
#endif
    switch (env->fp_status.float_rounding_mode) {
    case float_round_nearest_even:
        ret |= 2ULL << 58;
        break;
    case float_round_down:
        ret |= 1ULL << 58;
        break;
    case float_round_up:
        ret |= 3ULL << 58;
        break;
    case float_round_to_zero:
        break;
    }
    return ret;
}

void helper_store_fpcr (uint64_t val)
{
#ifdef CONFIG_SOFTFLOAT
    set_float_exception_flags((val >> 52) & 0x3F, &FP_STATUS);
#endif
    switch ((val >> 58) & 3) {
    case 0:
        set_float_rounding_mode(float_round_to_zero, &FP_STATUS);
        break;
    case 1:
        set_float_rounding_mode(float_round_down, &FP_STATUS);
        break;
    case 2:
        set_float_rounding_mode(float_round_nearest_even, &FP_STATUS);
        break;
    case 3:
        set_float_rounding_mode(float_round_up, &FP_STATUS);
        break;
    }
}

spinlock_t intr_cpu_lock = SPIN_LOCK_UNLOCKED;

uint64_t helper_rs(void)
{
    uint64_t tmp;

    spin_lock(&intr_cpu_lock);
    tmp = env->intr_flag;
    env->intr_flag = 1;
    spin_unlock(&intr_cpu_lock);

    return tmp;
}

uint64_t helper_rc(void)
{
    uint64_t tmp;

    spin_lock(&intr_cpu_lock);
    tmp = env->intr_flag;
    env->intr_flag = 0;
    spin_unlock(&intr_cpu_lock);

    return tmp;
}

uint64_t helper_addqv (uint64_t op1, uint64_t op2)
{
    uint64_t tmp = op1;
    op1 += op2;
    if (unlikely((tmp ^ op2 ^ (-1ULL)) & (tmp ^ op1) & (1ULL << 63))) {
        helper_excp(EXCP_ARITH, EXCP_ARITH_OVERFLOW);
    }
    return op1;
}

uint64_t helper_addlv (uint64_t op1, uint64_t op2)
{
    uint64_t tmp = op1;
    op1 = (uint32_t)(op1 + op2);
    if (unlikely((tmp ^ op2 ^ (-1UL)) & (tmp ^ op1) & (1UL << 31))) {
        helper_excp(EXCP_ARITH, EXCP_ARITH_OVERFLOW);
    }
    return op1;
}

uint64_t helper_subqv (uint64_t op1, uint64_t op2)
{
    uint64_t tmp = op1;
    op1 -= op2;
    if (unlikely(((~tmp) ^ op1 ^ (-1ULL)) & ((~tmp) ^ op2) & (1ULL << 63))) {
        helper_excp(EXCP_ARITH, EXCP_ARITH_OVERFLOW);
    }
    return op1;
}

uint64_t helper_sublv (uint64_t op1, uint64_t op2)
{
    uint64_t tmp = op1;
    op1 = (uint32_t)(op1 - op2);
    if (unlikely(((~tmp) ^ op1 ^ (-1UL)) & ((~tmp) ^ op2) & (1UL << 31))) {
        helper_excp(EXCP_ARITH, EXCP_ARITH_OVERFLOW);
    }
    return op1;
}

uint64_t helper_mullv (uint64_t op1, uint64_t op2)
{
    int64_t res = (int64_t)op1 * (int64_t)op2;

    if (unlikely((int32_t)res != res)) {
        helper_excp(EXCP_ARITH, EXCP_ARITH_OVERFLOW);
    }
    return (int64_t)((int32_t)res);
}

uint64_t helper_mulqv (uint64_t op1, uint64_t op2)
{
    uint64_t tl, th;

    muls64(&tl, &th, op1, op2);
    /* If th != 0 && th != -1, then we had an overflow */
    if (unlikely((th + 1) > 1)) {
        helper_excp(EXCP_ARITH, EXCP_ARITH_OVERFLOW);
    }
    return tl;
}

uint64_t helper_umulh (uint64_t op1, uint64_t op2)
{
    uint64_t tl, th;

    mulu64(&tl, &th, op1, op2);
    return th;
}

uint64_t helper_ctpop (uint64_t arg)
{
    return ctpop64(arg);
}

uint64_t helper_ctlz (uint64_t arg)
{
    return clz64(arg);
}

uint64_t helper_cttz (uint64_t arg)
{
    return ctz64(arg);
}

static always_inline uint64_t byte_zap (uint64_t op, uint8_t mskb)
{
    uint64_t mask;

    mask = 0;
    mask |= ((mskb >> 0) & 1) * 0x00000000000000FFULL;
    mask |= ((mskb >> 1) & 1) * 0x000000000000FF00ULL;
    mask |= ((mskb >> 2) & 1) * 0x0000000000FF0000ULL;
    mask |= ((mskb >> 3) & 1) * 0x00000000FF000000ULL;
    mask |= ((mskb >> 4) & 1) * 0x000000FF00000000ULL;
    mask |= ((mskb >> 5) & 1) * 0x0000FF0000000000ULL;
    mask |= ((mskb >> 6) & 1) * 0x00FF000000000000ULL;
    mask |= ((mskb >> 7) & 1) * 0xFF00000000000000ULL;

    return op & ~mask;
}

uint64_t helper_mskbl(uint64_t val, uint64_t mask)
{
    return byte_zap(val, 0x01 << (mask & 7));
}

uint64_t helper_insbl(uint64_t val, uint64_t mask)
{
    val <<= (mask & 7) * 8;
    return byte_zap(val, ~(0x01 << (mask & 7)));
}

uint64_t helper_mskwl(uint64_t val, uint64_t mask)
{
    return byte_zap(val, 0x03 << (mask & 7));
}

uint64_t helper_inswl(uint64_t val, uint64_t mask)
{
    val <<= (mask & 7) * 8;
    return byte_zap(val, ~(0x03 << (mask & 7)));
}

uint64_t helper_mskll(uint64_t val, uint64_t mask)
{
    return byte_zap(val, 0x0F << (mask & 7));
}

uint64_t helper_insll(uint64_t val, uint64_t mask)
{
    val <<= (mask & 7) * 8;
    return byte_zap(val, ~(0x0F << (mask & 7)));
}

uint64_t helper_zap(uint64_t val, uint64_t mask)
{
    return byte_zap(val, mask);
}

uint64_t helper_zapnot(uint64_t val, uint64_t mask)
{
    return byte_zap(val, ~mask);
}

uint64_t helper_mskql(uint64_t val, uint64_t mask)
{
    return byte_zap(val, 0xFF << (mask & 7));
}

uint64_t helper_insql(uint64_t val, uint64_t mask)
{
    val <<= (mask & 7) * 8;
    return byte_zap(val, ~(0xFF << (mask & 7)));
}

uint64_t helper_mskwh(uint64_t val, uint64_t mask)
{
    return byte_zap(val, (0x03 << (mask & 7)) >> 8);
}

uint64_t helper_inswh(uint64_t val, uint64_t mask)
{
    val >>= 64 - ((mask & 7) * 8);
    return byte_zap(val, ~((0x03 << (mask & 7)) >> 8));
}

uint64_t helper_msklh(uint64_t val, uint64_t mask)
{
    return byte_zap(val, (0x0F << (mask & 7)) >> 8);
}

uint64_t helper_inslh(uint64_t val, uint64_t mask)
{
    val >>= 64 - ((mask & 7) * 8);
    return byte_zap(val, ~((0x0F << (mask & 7)) >> 8));
}

uint64_t helper_mskqh(uint64_t val, uint64_t mask)
{
    return byte_zap(val, (0xFF << (mask & 7)) >> 8);
}

uint64_t helper_insqh(uint64_t val, uint64_t mask)
{
    val >>= 64 - ((mask & 7) * 8);
    return byte_zap(val, ~((0xFF << (mask & 7)) >> 8));
}

uint64_t helper_cmpbge (uint64_t op1, uint64_t op2)
{
    uint8_t opa, opb, res;
    int i;

    res = 0;
    for (i = 0; i < 8; i++) {
        opa = op1 >> (i * 8);
        opb = op2 >> (i * 8);
        if (opa >= opb)
            res |= 1 << i;
    }
    return res;
}

/* Floating point helpers */

/* F floating (VAX) */
static always_inline uint64_t float32_to_f (float32 fa)
{
    uint64_t r, exp, mant, sig;
    CPU_FloatU a;

    a.f = fa;
    sig = ((uint64_t)a.l & 0x80000000) << 32;
    exp = (a.l >> 23) & 0xff;
    mant = ((uint64_t)a.l & 0x007fffff) << 29;

    if (exp == 255) {
        /* NaN or infinity */
        r = 1; /* VAX dirty zero */
    } else if (exp == 0) {
        if (mant == 0) {
            /* Zero */
            r = 0;
        } else {
            /* Denormalized */
            r = sig | ((exp + 1) << 52) | mant;
        }
    } else {
        if (exp >= 253) {
            /* Overflow */
            r = 1; /* VAX dirty zero */
        } else {
            r = sig | ((exp + 2) << 52);
        }
    }

    return r;
}

static always_inline float32 f_to_float32 (uint64_t a)
{
    uint32_t exp, mant_sig;
    CPU_FloatU r;

    exp = ((a >> 55) & 0x80) | ((a >> 52) & 0x7f);
    mant_sig = ((a >> 32) & 0x80000000) | ((a >> 29) & 0x007fffff);

    if (unlikely(!exp && mant_sig)) {
        /* Reserved operands / Dirty zero */
        helper_excp(EXCP_OPCDEC, 0);
    }

    if (exp < 3) {
        /* Underflow */
        r.l = 0;
    } else {
        r.l = ((exp - 2) << 23) | mant_sig;
    }

    return r.f;
}

uint32_t helper_f_to_memory (uint64_t a)
{
    uint32_t r;
    r =  (a & 0x00001fffe0000000ull) >> 13;
    r |= (a & 0x07ffe00000000000ull) >> 45;
    r |= (a & 0xc000000000000000ull) >> 48;
    return r;
}

uint64_t helper_memory_to_f (uint32_t a)
{
    uint64_t r;
    r =  ((uint64_t)(a & 0x0000c000)) << 48;
    r |= ((uint64_t)(a & 0x003fffff)) << 45;
    r |= ((uint64_t)(a & 0xffff0000)) << 13;
    if (!(a & 0x00004000))
        r |= 0x7ll << 59;
    return r;
}

uint64_t helper_addf (uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = f_to_float32(a);
    fb = f_to_float32(b);
    fr = float32_add(fa, fb, &FP_STATUS);
    return float32_to_f(fr);
}

uint64_t helper_subf (uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = f_to_float32(a);
    fb = f_to_float32(b);
    fr = float32_sub(fa, fb, &FP_STATUS);
    return float32_to_f(fr);
}

uint64_t helper_mulf (uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = f_to_float32(a);
    fb = f_to_float32(b);
    fr = float32_mul(fa, fb, &FP_STATUS);
    return float32_to_f(fr);
}

uint64_t helper_divf (uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = f_to_float32(a);
    fb = f_to_float32(b);
    fr = float32_div(fa, fb, &FP_STATUS);
    return float32_to_f(fr);
}

uint64_t helper_sqrtf (uint64_t t)
{
    float32 ft, fr;

    ft = f_to_float32(t);
    fr = float32_sqrt(ft, &FP_STATUS);
    return float32_to_f(fr);
}


/* G floating (VAX) */
static always_inline uint64_t float64_to_g (float64 fa)
{
    uint64_t r, exp, mant, sig;
    CPU_DoubleU a;

    a.d = fa;
    sig = a.ll & 0x8000000000000000ull;
    exp = (a.ll >> 52) & 0x7ff;
    mant = a.ll & 0x000fffffffffffffull;

    if (exp == 2047) {
        /* NaN or infinity */
        r = 1; /* VAX dirty zero */
    } else if (exp == 0) {
        if (mant == 0) {
            /* Zero */
            r = 0;
        } else {
            /* Denormalized */
            r = sig | ((exp + 1) << 52) | mant;
        }
    } else {
        if (exp >= 2045) {
            /* Overflow */
            r = 1; /* VAX dirty zero */
        } else {
            r = sig | ((exp + 2) << 52);
        }
    }

    return r;
}

static always_inline float64 g_to_float64 (uint64_t a)
{
    uint64_t exp, mant_sig;
    CPU_DoubleU r;

    exp = (a >> 52) & 0x7ff;
    mant_sig = a & 0x800fffffffffffffull;

    if (!exp && mant_sig) {
        /* Reserved operands / Dirty zero */
        helper_excp(EXCP_OPCDEC, 0);
    }

    if (exp < 3) {
        /* Underflow */
        r.ll = 0;
    } else {
        r.ll = ((exp - 2) << 52) | mant_sig;
    }

    return r.d;
}

uint64_t helper_g_to_memory (uint64_t a)
{
    uint64_t r;
    r =  (a & 0x000000000000ffffull) << 48;
    r |= (a & 0x00000000ffff0000ull) << 16;
    r |= (a & 0x0000ffff00000000ull) >> 16;
    r |= (a & 0xffff000000000000ull) >> 48;
    return r;
}

uint64_t helper_memory_to_g (uint64_t a)
{
    uint64_t r;
    r =  (a & 0x000000000000ffffull) << 48;
    r |= (a & 0x00000000ffff0000ull) << 16;
    r |= (a & 0x0000ffff00000000ull) >> 16;
    r |= (a & 0xffff000000000000ull) >> 48;
    return r;
}

uint64_t helper_addg (uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = g_to_float64(a);
    fb = g_to_float64(b);
    fr = float64_add(fa, fb, &FP_STATUS);
    return float64_to_g(fr);
}

uint64_t helper_subg (uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = g_to_float64(a);
    fb = g_to_float64(b);
    fr = float64_sub(fa, fb, &FP_STATUS);
    return float64_to_g(fr);
}

uint64_t helper_mulg (uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = g_to_float64(a);
    fb = g_to_float64(b);
    fr = float64_mul(fa, fb, &FP_STATUS);
    return float64_to_g(fr);
}

uint64_t helper_divg (uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = g_to_float64(a);
    fb = g_to_float64(b);
    fr = float64_div(fa, fb, &FP_STATUS);
    return float64_to_g(fr);
}

uint64_t helper_sqrtg (uint64_t a)
{
    float64 fa, fr;

    fa = g_to_float64(a);
    fr = float64_sqrt(fa, &FP_STATUS);
    return float64_to_g(fr);
}


/* S floating (single) */
static always_inline uint64_t float32_to_s (float32 fa)
{
    CPU_FloatU a;
    uint64_t r;

    a.f = fa;

    r = (((uint64_t)(a.l & 0xc0000000)) << 32) | (((uint64_t)(a.l & 0x3fffffff)) << 29);
    if (((a.l & 0x7f800000) != 0x7f800000) && (!(a.l & 0x40000000)))
        r |= 0x7ll << 59;
    return r;
}

static always_inline float32 s_to_float32 (uint64_t a)
{
    CPU_FloatU r;
    r.l = ((a >> 32) & 0xc0000000) | ((a >> 29) & 0x3fffffff);
    return r.f;
}

uint32_t helper_s_to_memory (uint64_t a)
{
    /* Memory format is the same as float32 */
    float32 fa = s_to_float32(a);
    return *(uint32_t*)(&fa);
}

uint64_t helper_memory_to_s (uint32_t a)
{
    /* Memory format is the same as float32 */
    return float32_to_s(*(float32*)(&a));
}

uint64_t helper_adds (uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = s_to_float32(a);
    fb = s_to_float32(b);
    fr = float32_add(fa, fb, &FP_STATUS);
    return float32_to_s(fr);
}

uint64_t helper_subs (uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = s_to_float32(a);
    fb = s_to_float32(b);
    fr = float32_sub(fa, fb, &FP_STATUS);
    return float32_to_s(fr);
}

uint64_t helper_muls (uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = s_to_float32(a);
    fb = s_to_float32(b);
    fr = float32_mul(fa, fb, &FP_STATUS);
    return float32_to_s(fr);
}

uint64_t helper_divs (uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = s_to_float32(a);
    fb = s_to_float32(b);
    fr = float32_div(fa, fb, &FP_STATUS);
    return float32_to_s(fr);
}

uint64_t helper_sqrts (uint64_t a)
{
    float32 fa, fr;

    fa = s_to_float32(a);
    fr = float32_sqrt(fa, &FP_STATUS);
    return float32_to_s(fr);
}


/* T floating (double) */
static always_inline float64 t_to_float64 (uint64_t a)
{
    /* Memory format is the same as float64 */
    CPU_DoubleU r;
    r.ll = a;
    return r.d;
}

static always_inline uint64_t float64_to_t (float64 fa)
{
    /* Memory format is the same as float64 */
    CPU_DoubleU r;
    r.d = fa;
    return r.ll;
}

uint64_t helper_addt (uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = t_to_float64(a);
    fb = t_to_float64(b);
    fr = float64_add(fa, fb, &FP_STATUS);
    return float64_to_t(fr);
}

uint64_t helper_subt (uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = t_to_float64(a);
    fb = t_to_float64(b);
    fr = float64_sub(fa, fb, &FP_STATUS);
    return float64_to_t(fr);
}

uint64_t helper_mult (uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = t_to_float64(a);
    fb = t_to_float64(b);
    fr = float64_mul(fa, fb, &FP_STATUS);
    return float64_to_t(fr);
}

uint64_t helper_divt (uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = t_to_float64(a);
    fb = t_to_float64(b);
    fr = float64_div(fa, fb, &FP_STATUS);
    return float64_to_t(fr);
}

uint64_t helper_sqrtt (uint64_t a)
{
    float64 fa, fr;

    fa = t_to_float64(a);
    fr = float64_sqrt(fa, &FP_STATUS);
    return float64_to_t(fr);
}


/* Sign copy */
uint64_t helper_cpys(uint64_t a, uint64_t b)
{
    return (a & 0x8000000000000000ULL) | (b & ~0x8000000000000000ULL);
}

uint64_t helper_cpysn(uint64_t a, uint64_t b)
{
    return ((~a) & 0x8000000000000000ULL) | (b & ~0x8000000000000000ULL);
}

uint64_t helper_cpyse(uint64_t a, uint64_t b)
{
    return (a & 0xFFF0000000000000ULL) | (b & ~0xFFF0000000000000ULL);
}


/* Comparisons */
uint64_t helper_cmptun (uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = t_to_float64(a);
    fb = t_to_float64(b);

    if (float64_is_nan(fa) || float64_is_nan(fb))
        return 0x4000000000000000ULL;
    else
        return 0;
}

uint64_t helper_cmpteq(uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = t_to_float64(a);
    fb = t_to_float64(b);

    if (float64_eq(fa, fb, &FP_STATUS))
        return 0x4000000000000000ULL;
    else
        return 0;
}

uint64_t helper_cmptle(uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = t_to_float64(a);
    fb = t_to_float64(b);

    if (float64_le(fa, fb, &FP_STATUS))
        return 0x4000000000000000ULL;
    else
        return 0;
}

uint64_t helper_cmptlt(uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = t_to_float64(a);
    fb = t_to_float64(b);

    if (float64_lt(fa, fb, &FP_STATUS))
        return 0x4000000000000000ULL;
    else
        return 0;
}

uint64_t helper_cmpgeq(uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = g_to_float64(a);
    fb = g_to_float64(b);

    if (float64_eq(fa, fb, &FP_STATUS))
        return 0x4000000000000000ULL;
    else
        return 0;
}

uint64_t helper_cmpgle(uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = g_to_float64(a);
    fb = g_to_float64(b);

    if (float64_le(fa, fb, &FP_STATUS))
        return 0x4000000000000000ULL;
    else
        return 0;
}

uint64_t helper_cmpglt(uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = g_to_float64(a);
    fb = g_to_float64(b);

    if (float64_lt(fa, fb, &FP_STATUS))
        return 0x4000000000000000ULL;
    else
        return 0;
}

uint64_t helper_cmpfeq (uint64_t a)
{
    return !(a & 0x7FFFFFFFFFFFFFFFULL);
}

uint64_t helper_cmpfne (uint64_t a)
{
    return (a & 0x7FFFFFFFFFFFFFFFULL);
}

uint64_t helper_cmpflt (uint64_t a)
{
    return (a & 0x8000000000000000ULL) && (a & 0x7FFFFFFFFFFFFFFFULL);
}

uint64_t helper_cmpfle (uint64_t a)
{
    return (a & 0x8000000000000000ULL) || !(a & 0x7FFFFFFFFFFFFFFFULL);
}

uint64_t helper_cmpfgt (uint64_t a)
{
    return !(a & 0x8000000000000000ULL) && (a & 0x7FFFFFFFFFFFFFFFULL);
}

uint64_t helper_cmpfge (uint64_t a)
{
    return !(a & 0x8000000000000000ULL) || !(a & 0x7FFFFFFFFFFFFFFFULL);
}


/* Floating point format conversion */
uint64_t helper_cvtts (uint64_t a)
{
    float64 fa;
    float32 fr;

    fa = t_to_float64(a);
    fr = float64_to_float32(fa, &FP_STATUS);
    return float32_to_s(fr);
}

uint64_t helper_cvtst (uint64_t a)
{
    float32 fa;
    float64 fr;

    fa = s_to_float32(a);
    fr = float32_to_float64(fa, &FP_STATUS);
    return float64_to_t(fr);
}

uint64_t helper_cvtqs (uint64_t a)
{
    float32 fr = int64_to_float32(a, &FP_STATUS);
    return float32_to_s(fr);
}

uint64_t helper_cvttq (uint64_t a)
{
    float64 fa = t_to_float64(a);
    return float64_to_int64_round_to_zero(fa, &FP_STATUS);
}

uint64_t helper_cvtqt (uint64_t a)
{
    float64 fr = int64_to_float64(a, &FP_STATUS);
    return float64_to_t(fr);
}

uint64_t helper_cvtqf (uint64_t a)
{
    float32 fr = int64_to_float32(a, &FP_STATUS);
    return float32_to_f(fr);
}

uint64_t helper_cvtgf (uint64_t a)
{
    float64 fa;
    float32 fr;

    fa = g_to_float64(a);
    fr = float64_to_float32(fa, &FP_STATUS);
    return float32_to_f(fr);
}

uint64_t helper_cvtgq (uint64_t a)
{
    float64 fa = g_to_float64(a);
    return float64_to_int64_round_to_zero(fa, &FP_STATUS);
}

uint64_t helper_cvtqg (uint64_t a)
{
    float64 fr;
    fr = int64_to_float64(a, &FP_STATUS);
    return float64_to_g(fr);
}

uint64_t helper_cvtlq (uint64_t a)
{
    return (int64_t)((int32_t)((a >> 32) | ((a >> 29) & 0x3FFFFFFF)));
}

static always_inline uint64_t __helper_cvtql (uint64_t a, int s, int v)
{
    uint64_t r;

    r = ((uint64_t)(a & 0xC0000000)) << 32;
    r |= ((uint64_t)(a & 0x7FFFFFFF)) << 29;

    if (v && (int64_t)((int32_t)r) != (int64_t)r) {
        helper_excp(EXCP_ARITH, EXCP_ARITH_OVERFLOW);
    }
    if (s) {
        /* TODO */
    }
    return r;
}

uint64_t helper_cvtql (uint64_t a)
{
    return __helper_cvtql(a, 0, 0);
}

uint64_t helper_cvtqlv (uint64_t a)
{
    return __helper_cvtql(a, 0, 1);
}

uint64_t helper_cvtqlsv (uint64_t a)
{
    return __helper_cvtql(a, 1, 1);
}

/* PALcode support special instructions */
#if !defined (CONFIG_USER_ONLY)
void helper_hw_rei (void)
{
    env->pc = env->ipr[IPR_EXC_ADDR] & ~3;
    env->ipr[IPR_EXC_ADDR] = env->ipr[IPR_EXC_ADDR] & 1;
    /* XXX: re-enable interrupts and memory mapping */
}

void helper_hw_ret (uint64_t a)
{
    env->pc = a & ~3;
    env->ipr[IPR_EXC_ADDR] = a & 1;
    /* XXX: re-enable interrupts and memory mapping */
}

uint64_t helper_mfpr (int iprn, uint64_t val)
{
    uint64_t tmp;

    if (cpu_alpha_mfpr(env, iprn, &tmp) == 0)
        val = tmp;

    return val;
}

void helper_mtpr (int iprn, uint64_t val)
{
    cpu_alpha_mtpr(env, iprn, val, NULL);
}

void helper_set_alt_mode (void)
{
    env->saved_mode = env->ps & 0xC;
    env->ps = (env->ps & ~0xC) | (env->ipr[IPR_ALT_MODE] & 0xC);
}

void helper_restore_mode (void)
{
    env->ps = (env->ps & ~0xC) | env->saved_mode;
}

#endif

/*****************************************************************************/
/* Softmmu support */
#if !defined (CONFIG_USER_ONLY)

/* XXX: the two following helpers are pure hacks.
 *      Hopefully, we emulate the PALcode, then we should never see
 *      HW_LD / HW_ST instructions.
 */
uint64_t helper_ld_virt_to_phys (uint64_t virtaddr)
{
    uint64_t tlb_addr, physaddr;
    int index, mmu_idx;
    void *retaddr;

    mmu_idx = cpu_mmu_index(env);
    index = (virtaddr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
 redo:
    tlb_addr = env->tlb_table[mmu_idx][index].addr_read;
    if ((virtaddr & TARGET_PAGE_MASK) ==
        (tlb_addr & (TARGET_PAGE_MASK | TLB_INVALID_MASK))) {
        physaddr = virtaddr + env->tlb_table[mmu_idx][index].addend;
    } else {
        /* the page is not in the TLB : fill it */
        retaddr = GETPC();
        tlb_fill(virtaddr, 0, mmu_idx, retaddr);
        goto redo;
    }
    return physaddr;
}

uint64_t helper_st_virt_to_phys (uint64_t virtaddr)
{
    uint64_t tlb_addr, physaddr;
    int index, mmu_idx;
    void *retaddr;

    mmu_idx = cpu_mmu_index(env);
    index = (virtaddr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
 redo:
    tlb_addr = env->tlb_table[mmu_idx][index].addr_write;
    if ((virtaddr & TARGET_PAGE_MASK) ==
        (tlb_addr & (TARGET_PAGE_MASK | TLB_INVALID_MASK))) {
        physaddr = virtaddr + env->tlb_table[mmu_idx][index].addend;
    } else {
        /* the page is not in the TLB : fill it */
        retaddr = GETPC();
        tlb_fill(virtaddr, 1, mmu_idx, retaddr);
        goto redo;
    }
    return physaddr;
}

void helper_ldl_raw(uint64_t t0, uint64_t t1)
{
    ldl_raw(t1, t0);
}

void helper_ldq_raw(uint64_t t0, uint64_t t1)
{
    ldq_raw(t1, t0);
}

void helper_ldl_l_raw(uint64_t t0, uint64_t t1)
{
    env->lock = t1;
    ldl_raw(t1, t0);
}

void helper_ldq_l_raw(uint64_t t0, uint64_t t1)
{
    env->lock = t1;
    ldl_raw(t1, t0);
}

void helper_ldl_kernel(uint64_t t0, uint64_t t1)
{
    ldl_kernel(t1, t0);
}

void helper_ldq_kernel(uint64_t t0, uint64_t t1)
{
    ldq_kernel(t1, t0);
}

void helper_ldl_data(uint64_t t0, uint64_t t1)
{
    ldl_data(t1, t0);
}

void helper_ldq_data(uint64_t t0, uint64_t t1)
{
    ldq_data(t1, t0);
}

void helper_stl_raw(uint64_t t0, uint64_t t1)
{
    stl_raw(t1, t0);
}

void helper_stq_raw(uint64_t t0, uint64_t t1)
{
    stq_raw(t1, t0);
}

uint64_t helper_stl_c_raw(uint64_t t0, uint64_t t1)
{
    uint64_t ret;

    if (t1 == env->lock) {
        stl_raw(t1, t0);
        ret = 0;
    } else
        ret = 1;

    env->lock = 1;

    return ret;
}

uint64_t helper_stq_c_raw(uint64_t t0, uint64_t t1)
{
    uint64_t ret;

    if (t1 == env->lock) {
        stq_raw(t1, t0);
        ret = 0;
    } else
        ret = 1;

    env->lock = 1;

    return ret;
}

#define MMUSUFFIX _mmu

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
    ret = cpu_alpha_handle_mmu_fault(env, addr, is_write, mmu_idx, 1);
    if (!likely(ret == 0)) {
        if (likely(retaddr)) {
            /* now we have a real cpu fault */
            pc = (unsigned long)retaddr;
            tb = tb_find_pc(pc);
            if (likely(tb)) {
                /* the PC is inside the translated code. It means that we have
                   a virtual CPU fault */
                cpu_restore_state(tb, env, pc, NULL);
            }
        }
        /* Exception index and error code are already set */
        cpu_loop_exit();
    }
    env = saved_env;
}

#endif
