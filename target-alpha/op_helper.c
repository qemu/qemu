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

#include "op_helper.h"

#define MEMSUFFIX _raw
#include "op_helper_mem.h"

#if !defined(CONFIG_USER_ONLY)
#define MEMSUFFIX _kernel
#include "op_helper_mem.h"

#define MEMSUFFIX _executive
#include "op_helper_mem.h"

#define MEMSUFFIX _supervisor
#include "op_helper_mem.h"

#define MEMSUFFIX _user
#include "op_helper_mem.h"

/* This is used for pal modes */
#define MEMSUFFIX _data
#include "op_helper_mem.h"
#endif

void helper_tb_flush (void)
{
    tlb_flush(env, 1);
}

void cpu_dump_EA (target_ulong EA);
void helper_print_mem_EA (target_ulong EA)
{
    cpu_dump_EA(EA);
}

/*****************************************************************************/
/* Exceptions processing helpers */
void helper_excp (uint32_t excp, uint32_t error)
{
    env->exception_index = excp;
    env->error_code = error;
    cpu_loop_exit();
}

void helper_amask (void)
{
    switch (env->implver) {
    case IMPLVER_2106x:
        /* EV4, EV45, LCA, LCA45 & EV5 */
        break;
    case IMPLVER_21164:
    case IMPLVER_21264:
    case IMPLVER_21364:
        T0 &= ~env->amask;
        break;
    }
}

void helper_load_pcc (void)
{
    /* XXX: TODO */
    T0 = 0;
}

void helper_load_implver (void)
{
    T0 = env->implver;
}

void helper_load_fpcr (void)
{
    T0 = 0;
#ifdef CONFIG_SOFTFLOAT
    T0 |= env->fp_status.float_exception_flags << 52;
    if (env->fp_status.float_exception_flags)
        T0 |= 1ULL << 63;
    env->ipr[IPR_EXC_SUM] &= ~0x3E:
    env->ipr[IPR_EXC_SUM] |= env->fp_status.float_exception_flags << 1;
#endif
    switch (env->fp_status.float_rounding_mode) {
    case float_round_nearest_even:
        T0 |= 2ULL << 58;
        break;
    case float_round_down:
        T0 |= 1ULL << 58;
        break;
    case float_round_up:
        T0 |= 3ULL << 58;
        break;
    case float_round_to_zero:
        break;
    }
}

void helper_store_fpcr (void)
{
#ifdef CONFIG_SOFTFLOAT
    set_float_exception_flags((T0 >> 52) & 0x3F, &FP_STATUS);
#endif
    switch ((T0 >> 58) & 3) {
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

void helper_load_irf (void)
{
    /* XXX: TODO */
    T0 = 0;
}

void helper_set_irf (void)
{
    /* XXX: TODO */
}

void helper_clear_irf (void)
{
    /* XXX: TODO */
}

void helper_addqv (void)
{
    T2 = T0;
    T0 += T1;
    if (unlikely((T2 ^ T1 ^ (-1ULL)) & (T2 ^ T0) & (1ULL << 63))) {
        helper_excp(EXCP_ARITH, EXCP_ARITH_OVERFLOW);
    }
}

void helper_addlv (void)
{
    T2 = T0;
    T0 = (uint32_t)(T0 + T1);
    if (unlikely((T2 ^ T1 ^ (-1UL)) & (T2 ^ T0) & (1UL << 31))) {
        helper_excp(EXCP_ARITH, EXCP_ARITH_OVERFLOW);
    }
}

void helper_subqv (void)
{
    T2 = T0;
    T0 -= T1;
    if (unlikely(((~T2) ^ T0 ^ (-1ULL)) & ((~T2) ^ T1) & (1ULL << 63))) {
        helper_excp(EXCP_ARITH, EXCP_ARITH_OVERFLOW);
    }
}

void helper_sublv (void)
{
    T2 = T0;
    T0 = (uint32_t)(T0 - T1);
    if (unlikely(((~T2) ^ T0 ^ (-1UL)) & ((~T2) ^ T1) & (1UL << 31))) {
        helper_excp(EXCP_ARITH, EXCP_ARITH_OVERFLOW);
    }
}

void helper_mullv (void)
{
    int64_t res = (int64_t)T0 * (int64_t)T1;

    if (unlikely((int32_t)res != res)) {
        helper_excp(EXCP_ARITH, EXCP_ARITH_OVERFLOW);
    }
    T0 = (int64_t)((int32_t)res);
}

void helper_mulqv ()
{
    uint64_t tl, th;

    muls64(&tl, &th, T0, T1);
    /* If th != 0 && th != -1, then we had an overflow */
    if (unlikely((th + 1) > 1)) {
        helper_excp(EXCP_ARITH, EXCP_ARITH_OVERFLOW);
    }
    T0 = tl;
}

void helper_ctpop (void)
{
    T0 = ctpop64(T0);
}

void helper_ctlz (void)
{
    T0 = clz64(T0);
}

void helper_cttz (void)
{
    T0 = ctz64(T0);
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

void helper_mskbl (void)
{
    T0 = byte_zap(T0, 0x01 << (T1 & 7));
}

void helper_extbl (void)
{
    T0 >>= (T1 & 7) * 8;
    T0 = byte_zap(T0, 0xFE);
}

void helper_insbl (void)
{
    T0 <<= (T1 & 7) * 8;
    T0 = byte_zap(T0, ~(0x01 << (T1 & 7)));
}

void helper_mskwl (void)
{
    T0 = byte_zap(T0, 0x03 << (T1 & 7));
}

void helper_extwl (void)
{
    T0 >>= (T1 & 7) * 8;
    T0 = byte_zap(T0, 0xFC);
}

void helper_inswl (void)
{
    T0 <<= (T1 & 7) * 8;
    T0 = byte_zap(T0, ~(0x03 << (T1 & 7)));
}

void helper_mskll (void)
{
    T0 = byte_zap(T0, 0x0F << (T1 & 7));
}

void helper_extll (void)
{
    T0 >>= (T1 & 7) * 8;
    T0 = byte_zap(T0, 0xF0);
}

void helper_insll (void)
{
    T0 <<= (T1 & 7) * 8;
    T0 = byte_zap(T0, ~(0x0F << (T1 & 7)));
}

void helper_zap (void)
{
    T0 = byte_zap(T0, T1);
}

void helper_zapnot (void)
{
    T0 = byte_zap(T0, ~T1);
}

void helper_mskql (void)
{
    T0 = byte_zap(T0, 0xFF << (T1 & 7));
}

void helper_extql (void)
{
    T0 >>= (T1 & 7) * 8;
    T0 = byte_zap(T0, 0x00);
}

void helper_insql (void)
{
    T0 <<= (T1 & 7) * 8;
    T0 = byte_zap(T0, ~(0xFF << (T1 & 7)));
}

void helper_mskwh (void)
{
    T0 = byte_zap(T0, (0x03 << (T1 & 7)) >> 8);
}

void helper_inswh (void)
{
    T0 >>= 64 - ((T1 & 7) * 8);
    T0 = byte_zap(T0, ~((0x03 << (T1 & 7)) >> 8));
}

void helper_extwh (void)
{
    T0 <<= 64 - ((T1 & 7) * 8);
    T0 = byte_zap(T0, ~0x07);
}

void helper_msklh (void)
{
    T0 = byte_zap(T0, (0x0F << (T1 & 7)) >> 8);
}

void helper_inslh (void)
{
    T0 >>= 64 - ((T1 & 7) * 8);
    T0 = byte_zap(T0, ~((0x0F << (T1 & 7)) >> 8));
}

void helper_extlh (void)
{
    T0 <<= 64 - ((T1 & 7) * 8);
    T0 = byte_zap(T0, ~0x0F);
}

void helper_mskqh (void)
{
    T0 = byte_zap(T0, (0xFF << (T1 & 7)) >> 8);
}

void helper_insqh (void)
{
    T0 >>= 64 - ((T1 & 7) * 8);
    T0 = byte_zap(T0, ~((0xFF << (T1 & 7)) >> 8));
}

void helper_extqh (void)
{
    T0 <<= 64 - ((T1 & 7) * 8);
    T0 = byte_zap(T0, 0x00);
}

void helper_cmpbge (void)
{
    uint8_t opa, opb, res;
    int i;

    res = 0;
    for (i = 0; i < 7; i++) {
        opa = T0 >> (i * 8);
        opb = T1 >> (i * 8);
        if (opa >= opb)
            res |= 1 << i;
    }
    T0 = res;
}

void helper_cmov_fir (int freg)
{
    if (FT0 != 0)
        env->fir[freg] = FT1;
}

void helper_sqrts (void)
{
    FT0 = float32_sqrt(FT0, &FP_STATUS);
}

void helper_cpys (void)
{
    union {
        double d;
        uint64_t i;
    } p, q, r;

    p.d = FT0;
    q.d = FT1;
    r.i = p.i & 0x8000000000000000ULL;
    r.i |= q.i & ~0x8000000000000000ULL;
    FT0 = r.d;
}

void helper_cpysn (void)
{
    union {
        double d;
        uint64_t i;
    } p, q, r;

    p.d = FT0;
    q.d = FT1;
    r.i = (~p.i) & 0x8000000000000000ULL;
    r.i |= q.i & ~0x8000000000000000ULL;
    FT0 = r.d;
}

void helper_cpyse (void)
{
    union {
        double d;
        uint64_t i;
    } p, q, r;

    p.d = FT0;
    q.d = FT1;
    r.i = p.i & 0xFFF0000000000000ULL;
    r.i |= q.i & ~0xFFF0000000000000ULL;
    FT0 = r.d;
}

void helper_itofs (void)
{
    union {
        double d;
        uint64_t i;
    } p;

    p.d = FT0;
    FT0 = int64_to_float32(p.i, &FP_STATUS);
}

void helper_ftois (void)
{
    union {
        double d;
        uint64_t i;
    } p;

    p.i = float32_to_int64(FT0, &FP_STATUS);
    FT0 = p.d;
}

void helper_sqrtt (void)
{
    FT0 = float64_sqrt(FT0, &FP_STATUS);
}

void helper_cmptun (void)
{
    union {
        double d;
        uint64_t i;
    } p;

    p.i = 0;
    if (float64_is_nan(FT0) || float64_is_nan(FT1))
        p.i = 0x4000000000000000ULL;
    FT0 = p.d;
}

void helper_cmpteq (void)
{
    union {
        double d;
        uint64_t i;
    } p;

    p.i = 0;
    if (float64_eq(FT0, FT1, &FP_STATUS))
        p.i = 0x4000000000000000ULL;
    FT0 = p.d;
}

void helper_cmptle (void)
{
    union {
        double d;
        uint64_t i;
    } p;

    p.i = 0;
    if (float64_le(FT0, FT1, &FP_STATUS))
        p.i = 0x4000000000000000ULL;
    FT0 = p.d;
}

void helper_cmptlt (void)
{
    union {
        double d;
        uint64_t i;
    } p;

    p.i = 0;
    if (float64_lt(FT0, FT1, &FP_STATUS))
        p.i = 0x4000000000000000ULL;
    FT0 = p.d;
}

void helper_itoft (void)
{
    union {
        double d;
        uint64_t i;
    } p;

    p.d = FT0;
    FT0 = int64_to_float64(p.i, &FP_STATUS);
}

void helper_ftoit (void)
{
    union {
        double d;
        uint64_t i;
    } p;

    p.i = float64_to_int64(FT0, &FP_STATUS);
    FT0 = p.d;
}

static always_inline int vaxf_is_valid (float ff)
{
    union {
        float f;
        uint32_t i;
    } p;
    uint32_t exp, mant;

    p.f = ff;
    exp = (p.i >> 23) & 0xFF;
    mant = p.i & 0x007FFFFF;
    if (exp == 0 && ((p.i & 0x80000000) || mant != 0)) {
        /* Reserved operands / Dirty zero */
        return 0;
    }

    return 1;
}

static always_inline float vaxf_to_ieee32 (float ff)
{
    union {
        float f;
        uint32_t i;
    } p;
    uint32_t exp;

    p.f = ff;
    exp = (p.i >> 23) & 0xFF;
    if (exp < 3) {
        /* Underflow */
        p.f = 0.0;
    } else {
        p.f *= 0.25;
    }

    return p.f;
}

static always_inline float ieee32_to_vaxf (float fi)
{
    union {
        float f;
        uint32_t i;
    } p;
    uint32_t exp, mant;

    p.f = fi;
    exp = (p.i >> 23) & 0xFF;
    mant = p.i & 0x007FFFFF;
    if (exp == 255) {
        /* NaN or infinity */
        p.i = 1;
    } else if (exp == 0) {
        if (mant == 0) {
            /* Zero */
            p.i = 0;
        } else {
            /* Denormalized */
            p.f *= 2.0;
        }
    } else {
        if (exp >= 253) {
            /* Overflow */
            p.i = 1;
        } else {
            p.f *= 4.0;
        }
    }

    return p.f;
}

void helper_addf (void)
{
    float ft0, ft1, ft2;

    if (!vaxf_is_valid(FT0) || !vaxf_is_valid(FT1)) {
        /* XXX: TODO */
    }
    ft0 = vaxf_to_ieee32(FT0);
    ft1 = vaxf_to_ieee32(FT1);
    ft2 = float32_add(ft0, ft1, &FP_STATUS);
    FT0 = ieee32_to_vaxf(ft2);
}

void helper_subf (void)
{
    float ft0, ft1, ft2;

    if (!vaxf_is_valid(FT0) || !vaxf_is_valid(FT1)) {
        /* XXX: TODO */
    }
    ft0 = vaxf_to_ieee32(FT0);
    ft1 = vaxf_to_ieee32(FT1);
    ft2 = float32_sub(ft0, ft1, &FP_STATUS);
    FT0 = ieee32_to_vaxf(ft2);
}

void helper_mulf (void)
{
    float ft0, ft1, ft2;

    if (!vaxf_is_valid(FT0) || !vaxf_is_valid(FT1)) {
        /* XXX: TODO */
    }
    ft0 = vaxf_to_ieee32(FT0);
    ft1 = vaxf_to_ieee32(FT1);
    ft2 = float32_mul(ft0, ft1, &FP_STATUS);
    FT0 = ieee32_to_vaxf(ft2);
}

void helper_divf (void)
{
    float ft0, ft1, ft2;

    if (!vaxf_is_valid(FT0) || !vaxf_is_valid(FT1)) {
        /* XXX: TODO */
    }
    ft0 = vaxf_to_ieee32(FT0);
    ft1 = vaxf_to_ieee32(FT1);
    ft2 = float32_div(ft0, ft1, &FP_STATUS);
    FT0 = ieee32_to_vaxf(ft2);
}

void helper_sqrtf (void)
{
    float ft0, ft1;

    if (!vaxf_is_valid(FT0) || !vaxf_is_valid(FT1)) {
        /* XXX: TODO */
    }
    ft0 = vaxf_to_ieee32(FT0);
    ft1 = float32_sqrt(ft0, &FP_STATUS);
    FT0 = ieee32_to_vaxf(ft1);
}

void helper_itoff (void)
{
    /* XXX: TODO */
}

static always_inline int vaxg_is_valid (double ff)
{
    union {
        double f;
        uint64_t i;
    } p;
    uint64_t exp, mant;

    p.f = ff;
    exp = (p.i >> 52) & 0x7FF;
    mant = p.i & 0x000FFFFFFFFFFFFFULL;
    if (exp == 0 && ((p.i & 0x8000000000000000ULL) || mant != 0)) {
        /* Reserved operands / Dirty zero */
        return 0;
    }

    return 1;
}

static always_inline double vaxg_to_ieee64 (double fg)
{
    union {
        double f;
        uint64_t i;
    } p;
    uint32_t exp;

    p.f = fg;
    exp = (p.i >> 52) & 0x7FF;
    if (exp < 3) {
        /* Underflow */
        p.f = 0.0;
    } else {
        p.f *= 0.25;
    }

    return p.f;
}

static always_inline double ieee64_to_vaxg (double fi)
{
    union {
        double f;
        uint64_t i;
    } p;
    uint64_t mant;
    uint32_t exp;

    p.f = fi;
    exp = (p.i >> 52) & 0x7FF;
    mant = p.i & 0x000FFFFFFFFFFFFFULL;
    if (exp == 255) {
        /* NaN or infinity */
        p.i = 1; /* VAX dirty zero */
    } else if (exp == 0) {
        if (mant == 0) {
            /* Zero */
            p.i = 0;
        } else {
            /* Denormalized */
            p.f *= 2.0;
        }
    } else {
        if (exp >= 2045) {
            /* Overflow */
            p.i = 1; /* VAX dirty zero */
        } else {
            p.f *= 4.0;
        }
    }

    return p.f;
}

void helper_addg (void)
{
    double ft0, ft1, ft2;

    if (!vaxg_is_valid(FT0) || !vaxg_is_valid(FT1)) {
        /* XXX: TODO */
    }
    ft0 = vaxg_to_ieee64(FT0);
    ft1 = vaxg_to_ieee64(FT1);
    ft2 = float64_add(ft0, ft1, &FP_STATUS);
    FT0 = ieee64_to_vaxg(ft2);
}

void helper_subg (void)
{
    double ft0, ft1, ft2;

    if (!vaxg_is_valid(FT0) || !vaxg_is_valid(FT1)) {
        /* XXX: TODO */
    }
    ft0 = vaxg_to_ieee64(FT0);
    ft1 = vaxg_to_ieee64(FT1);
    ft2 = float64_sub(ft0, ft1, &FP_STATUS);
    FT0 = ieee64_to_vaxg(ft2);
}

void helper_mulg (void)
{
    double ft0, ft1, ft2;

    if (!vaxg_is_valid(FT0) || !vaxg_is_valid(FT1)) {
        /* XXX: TODO */
    }
    ft0 = vaxg_to_ieee64(FT0);
    ft1 = vaxg_to_ieee64(FT1);
    ft2 = float64_mul(ft0, ft1, &FP_STATUS);
    FT0 = ieee64_to_vaxg(ft2);
}

void helper_divg (void)
{
    double ft0, ft1, ft2;

    if (!vaxg_is_valid(FT0) || !vaxg_is_valid(FT1)) {
        /* XXX: TODO */
    }
    ft0 = vaxg_to_ieee64(FT0);
    ft1 = vaxg_to_ieee64(FT1);
    ft2 = float64_div(ft0, ft1, &FP_STATUS);
    FT0 = ieee64_to_vaxg(ft2);
}

void helper_sqrtg (void)
{
    double ft0, ft1;

    if (!vaxg_is_valid(FT0) || !vaxg_is_valid(FT1)) {
        /* XXX: TODO */
    }
    ft0 = vaxg_to_ieee64(FT0);
    ft1 = float64_sqrt(ft0, &FP_STATUS);
    FT0 = ieee64_to_vaxg(ft1);
}

void helper_cmpgeq (void)
{
    union {
        double d;
        uint64_t u;
    } p;
    double ft0, ft1;

    if (!vaxg_is_valid(FT0) || !vaxg_is_valid(FT1)) {
        /* XXX: TODO */
    }
    ft0 = vaxg_to_ieee64(FT0);
    ft1 = vaxg_to_ieee64(FT1);
    p.u = 0;
    if (float64_eq(ft0, ft1, &FP_STATUS))
        p.u = 0x4000000000000000ULL;
    FT0 = p.d;
}

void helper_cmpglt (void)
{
    union {
        double d;
        uint64_t u;
    } p;
    double ft0, ft1;

    if (!vaxg_is_valid(FT0) || !vaxg_is_valid(FT1)) {
        /* XXX: TODO */
    }
    ft0 = vaxg_to_ieee64(FT0);
    ft1 = vaxg_to_ieee64(FT1);
    p.u = 0;
    if (float64_lt(ft0, ft1, &FP_STATUS))
        p.u = 0x4000000000000000ULL;
    FT0 = p.d;
}

void helper_cmpgle (void)
{
    union {
        double d;
        uint64_t u;
    } p;
    double ft0, ft1;

    if (!vaxg_is_valid(FT0) || !vaxg_is_valid(FT1)) {
        /* XXX: TODO */
    }
    ft0 = vaxg_to_ieee64(FT0);
    ft1 = vaxg_to_ieee64(FT1);
    p.u = 0;
    if (float64_le(ft0, ft1, &FP_STATUS))
        p.u = 0x4000000000000000ULL;
    FT0 = p.d;
}

void helper_cvtqs (void)
{
    union {
        double d;
        uint64_t u;
    } p;

    p.d = FT0;
    FT0 = (float)p.u;
}

void helper_cvttq (void)
{
    union {
        double d;
        uint64_t u;
    } p;

    p.u = FT0;
    FT0 = p.d;
}

void helper_cvtqt (void)
{
    union {
        double d;
        uint64_t u;
    } p;

    p.d = FT0;
    FT0 = p.u;
}

void helper_cvtqf (void)
{
    union {
        double d;
        uint64_t u;
    } p;

    p.d = FT0;
    FT0 = ieee32_to_vaxf(p.u);
}

void helper_cvtgf (void)
{
    double ft0;

    ft0 = vaxg_to_ieee64(FT0);
    FT0 = ieee32_to_vaxf(ft0);
}

void helper_cvtgd (void)
{
    /* XXX: TODO */
}

void helper_cvtgq (void)
{
    union {
        double d;
        uint64_t u;
    } p;

    p.u = vaxg_to_ieee64(FT0);
    FT0 = p.d;
}

void helper_cvtqg (void)
{
    union {
        double d;
        uint64_t u;
    } p;

    p.d = FT0;
    FT0 = ieee64_to_vaxg(p.u);
}

void helper_cvtdg (void)
{
    /* XXX: TODO */
}

void helper_cvtlq (void)
{
    union {
        double d;
        uint64_t u;
    } p, q;

    p.d = FT0;
    q.u = (p.u >> 29) & 0x3FFFFFFF;
    q.u |= (p.u >> 32);
    q.u = (int64_t)((int32_t)q.u);
    FT0 = q.d;
}

static always_inline void __helper_cvtql (int s, int v)
{
    union {
        double d;
        uint64_t u;
    } p, q;

    p.d = FT0;
    q.u = ((uint64_t)(p.u & 0xC0000000)) << 32;
    q.u |= ((uint64_t)(p.u & 0x7FFFFFFF)) << 29;
    FT0 = q.d;
    if (v && (int64_t)((int32_t)p.u) != (int64_t)p.u) {
        helper_excp(EXCP_ARITH, EXCP_ARITH_OVERFLOW);
    }
    if (s) {
        /* TODO */
    }
}

void helper_cvtql (void)
{
    __helper_cvtql(0, 0);
}

void helper_cvtqlv (void)
{
    __helper_cvtql(0, 1);
}

void helper_cvtqlsv (void)
{
    __helper_cvtql(1, 1);
}

void helper_cmpfeq (void)
{
    if (float64_eq(FT0, FT1, &FP_STATUS))
        T0 = 1;
    else
        T0 = 0;
}

void helper_cmpfne (void)
{
    if (float64_eq(FT0, FT1, &FP_STATUS))
        T0 = 0;
    else
        T0 = 1;
}

void helper_cmpflt (void)
{
    if (float64_lt(FT0, FT1, &FP_STATUS))
        T0 = 1;
    else
        T0 = 0;
}

void helper_cmpfle (void)
{
    if (float64_lt(FT0, FT1, &FP_STATUS))
        T0 = 1;
    else
        T0 = 0;
}

void helper_cmpfgt (void)
{
    if (float64_le(FT0, FT1, &FP_STATUS))
        T0 = 0;
    else
        T0 = 1;
}

void helper_cmpfge (void)
{
    if (float64_lt(FT0, FT1, &FP_STATUS))
        T0 = 0;
    else
        T0 = 1;
}

#if !defined (CONFIG_USER_ONLY)
void helper_mfpr (int iprn)
{
    uint64_t val;

    if (cpu_alpha_mfpr(env, iprn, &val) == 0)
        T0 = val;
}

void helper_mtpr (int iprn)
{
    cpu_alpha_mtpr(env, iprn, T0, NULL);
}
#endif

#if defined(HOST_SPARC) || defined(HOST_SPARC64)
void helper_reset_FT0 (void)
{
    FT0 = 0;
}

void helper_reset_FT1 (void)
{
    FT1 = 0;
}

void helper_reset_FT2 (void)
{
    FT2 = 0;
}
#endif

/*****************************************************************************/
/* Softmmu support */
#if !defined (CONFIG_USER_ONLY)

#ifdef __s390__
# define GETPC() ((void*)((unsigned long)__builtin_return_address(0) & 0x7fffffffUL))
#else
# define GETPC() (__builtin_return_address(0))
#endif

/* XXX: the two following helpers are pure hacks.
 *      Hopefully, we emulate the PALcode, then we should never see
 *      HW_LD / HW_ST instructions.
 */
void helper_ld_phys_to_virt (void)
{
    uint64_t tlb_addr, physaddr;
    int index, mmu_idx;
    void *retaddr;

    mmu_idx = cpu_mmu_index(env);
    index = (T0 >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
 redo:
    tlb_addr = env->tlb_table[mmu_idx][index].addr_read;
    if ((T0 & TARGET_PAGE_MASK) ==
        (tlb_addr & (TARGET_PAGE_MASK | TLB_INVALID_MASK))) {
        physaddr = T0 + env->tlb_table[mmu_idx][index].addend;
    } else {
        /* the page is not in the TLB : fill it */
        retaddr = GETPC();
        tlb_fill(T0, 0, mmu_idx, retaddr);
        goto redo;
    }
    T0 = physaddr;
}

void helper_st_phys_to_virt (void)
{
    uint64_t tlb_addr, physaddr;
    int index, mmu_idx;
    void *retaddr;

    mmu_idx = cpu_mmu_index(env);
    index = (T0 >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
 redo:
    tlb_addr = env->tlb_table[mmu_idx][index].addr_write;
    if ((T0 & TARGET_PAGE_MASK) ==
        (tlb_addr & (TARGET_PAGE_MASK | TLB_INVALID_MASK))) {
        physaddr = T0 + env->tlb_table[mmu_idx][index].addend;
    } else {
        /* the page is not in the TLB : fill it */
        retaddr = GETPC();
        tlb_fill(T0, 1, mmu_idx, retaddr);
        goto redo;
    }
    T0 = physaddr;
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
