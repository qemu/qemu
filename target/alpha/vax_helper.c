/*
 *  Helpers for vax floating point instructions.
 *
 *  Copyright (c) 2007 Jocelyn Mayer
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
#include "fpu/softfloat.h"

#define FP_STATUS (env->fp_status)


/* F floating (VAX) */
static uint64_t float32_to_f(float32 fa)
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

static float32 f_to_float32(CPUAlphaState *env, uintptr_t retaddr, uint64_t a)
{
    uint32_t exp, mant_sig;
    CPU_FloatU r;

    exp = ((a >> 55) & 0x80) | ((a >> 52) & 0x7f);
    mant_sig = ((a >> 32) & 0x80000000) | ((a >> 29) & 0x007fffff);

    if (unlikely(!exp && mant_sig)) {
        /* Reserved operands / Dirty zero */
        dynamic_excp(env, retaddr, EXCP_OPCDEC, 0);
    }

    if (exp < 3) {
        /* Underflow */
        r.l = 0;
    } else {
        r.l = ((exp - 2) << 23) | mant_sig;
    }

    return r.f;
}

uint32_t helper_f_to_memory(uint64_t a)
{
    uint32_t r;
    r =  (a & 0x00001fffe0000000ull) >> 13;
    r |= (a & 0x07ffe00000000000ull) >> 45;
    r |= (a & 0xc000000000000000ull) >> 48;
    return r;
}

uint64_t helper_memory_to_f(uint32_t a)
{
    uint64_t r;
    r =  ((uint64_t)(a & 0x0000c000)) << 48;
    r |= ((uint64_t)(a & 0x003fffff)) << 45;
    r |= ((uint64_t)(a & 0xffff0000)) << 13;
    if (!(a & 0x00004000)) {
        r |= 0x7ll << 59;
    }
    return r;
}

/* ??? Emulating VAX arithmetic with IEEE arithmetic is wrong.  We should
   either implement VAX arithmetic properly or just signal invalid opcode.  */

uint64_t helper_addf(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = f_to_float32(env, GETPC(), a);
    fb = f_to_float32(env, GETPC(), b);
    fr = float32_add(fa, fb, &FP_STATUS);
    return float32_to_f(fr);
}

uint64_t helper_subf(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = f_to_float32(env, GETPC(), a);
    fb = f_to_float32(env, GETPC(), b);
    fr = float32_sub(fa, fb, &FP_STATUS);
    return float32_to_f(fr);
}

uint64_t helper_mulf(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = f_to_float32(env, GETPC(), a);
    fb = f_to_float32(env, GETPC(), b);
    fr = float32_mul(fa, fb, &FP_STATUS);
    return float32_to_f(fr);
}

uint64_t helper_divf(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = f_to_float32(env, GETPC(), a);
    fb = f_to_float32(env, GETPC(), b);
    fr = float32_div(fa, fb, &FP_STATUS);
    return float32_to_f(fr);
}

uint64_t helper_sqrtf(CPUAlphaState *env, uint64_t t)
{
    float32 ft, fr;

    ft = f_to_float32(env, GETPC(), t);
    fr = float32_sqrt(ft, &FP_STATUS);
    return float32_to_f(fr);
}


/* G floating (VAX) */
static uint64_t float64_to_g(float64 fa)
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

static float64 g_to_float64(CPUAlphaState *env, uintptr_t retaddr, uint64_t a)
{
    uint64_t exp, mant_sig;
    CPU_DoubleU r;

    exp = (a >> 52) & 0x7ff;
    mant_sig = a & 0x800fffffffffffffull;

    if (!exp && mant_sig) {
        /* Reserved operands / Dirty zero */
        dynamic_excp(env, retaddr, EXCP_OPCDEC, 0);
    }

    if (exp < 3) {
        /* Underflow */
        r.ll = 0;
    } else {
        r.ll = ((exp - 2) << 52) | mant_sig;
    }

    return r.d;
}

uint64_t helper_g_to_memory(uint64_t a)
{
    uint64_t r;
    r =  (a & 0x000000000000ffffull) << 48;
    r |= (a & 0x00000000ffff0000ull) << 16;
    r |= (a & 0x0000ffff00000000ull) >> 16;
    r |= (a & 0xffff000000000000ull) >> 48;
    return r;
}

uint64_t helper_memory_to_g(uint64_t a)
{
    uint64_t r;
    r =  (a & 0x000000000000ffffull) << 48;
    r |= (a & 0x00000000ffff0000ull) << 16;
    r |= (a & 0x0000ffff00000000ull) >> 16;
    r |= (a & 0xffff000000000000ull) >> 48;
    return r;
}

uint64_t helper_addg(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = g_to_float64(env, GETPC(), a);
    fb = g_to_float64(env, GETPC(), b);
    fr = float64_add(fa, fb, &FP_STATUS);
    return float64_to_g(fr);
}

uint64_t helper_subg(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = g_to_float64(env, GETPC(), a);
    fb = g_to_float64(env, GETPC(), b);
    fr = float64_sub(fa, fb, &FP_STATUS);
    return float64_to_g(fr);
}

uint64_t helper_mulg(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = g_to_float64(env, GETPC(), a);
    fb = g_to_float64(env, GETPC(), b);
    fr = float64_mul(fa, fb, &FP_STATUS);
    return float64_to_g(fr);
}

uint64_t helper_divg(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = g_to_float64(env, GETPC(), a);
    fb = g_to_float64(env, GETPC(), b);
    fr = float64_div(fa, fb, &FP_STATUS);
    return float64_to_g(fr);
}

uint64_t helper_sqrtg(CPUAlphaState *env, uint64_t a)
{
    float64 fa, fr;

    fa = g_to_float64(env, GETPC(), a);
    fr = float64_sqrt(fa, &FP_STATUS);
    return float64_to_g(fr);
}

uint64_t helper_cmpgeq(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = g_to_float64(env, GETPC(), a);
    fb = g_to_float64(env, GETPC(), b);

    if (float64_eq_quiet(fa, fb, &FP_STATUS)) {
        return 0x4000000000000000ULL;
    } else {
        return 0;
    }
}

uint64_t helper_cmpgle(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = g_to_float64(env, GETPC(), a);
    fb = g_to_float64(env, GETPC(), b);

    if (float64_le(fa, fb, &FP_STATUS)) {
        return 0x4000000000000000ULL;
    } else {
        return 0;
    }
}

uint64_t helper_cmpglt(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = g_to_float64(env, GETPC(), a);
    fb = g_to_float64(env, GETPC(), b);

    if (float64_lt(fa, fb, &FP_STATUS)) {
        return 0x4000000000000000ULL;
    } else {
        return 0;
    }
}

uint64_t helper_cvtqf(CPUAlphaState *env, uint64_t a)
{
    float32 fr = int64_to_float32(a, &FP_STATUS);
    return float32_to_f(fr);
}

uint64_t helper_cvtgf(CPUAlphaState *env, uint64_t a)
{
    float64 fa;
    float32 fr;

    fa = g_to_float64(env, GETPC(), a);
    fr = float64_to_float32(fa, &FP_STATUS);
    return float32_to_f(fr);
}

uint64_t helper_cvtgq(CPUAlphaState *env, uint64_t a)
{
    float64 fa = g_to_float64(env, GETPC(), a);
    return float64_to_int64_round_to_zero(fa, &FP_STATUS);
}

uint64_t helper_cvtqg(CPUAlphaState *env, uint64_t a)
{
    float64 fr;
    fr = int64_to_float64(a, &FP_STATUS);
    return float64_to_g(fr);
}
