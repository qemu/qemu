/*
 *  Helpers for floating point instructions.
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "cpu.h"
#include "helper.h"
#include "fpu/softfloat.h"

#define FP_STATUS (env->fp_status)


void helper_setroundmode(CPUAlphaState *env, uint32_t val)
{
    set_float_rounding_mode(val, &FP_STATUS);
}

void helper_setflushzero(CPUAlphaState *env, uint32_t val)
{
    set_flush_to_zero(val, &FP_STATUS);
}

void helper_fp_exc_clear(CPUAlphaState *env)
{
    set_float_exception_flags(0, &FP_STATUS);
}

uint32_t helper_fp_exc_get(CPUAlphaState *env)
{
    return get_float_exception_flags(&FP_STATUS);
}

static inline void inline_fp_exc_raise(CPUAlphaState *env, uintptr_t retaddr,
                                       uint32_t exc, uint32_t regno)
{
    if (exc) {
        uint32_t hw_exc = 0;

        if (exc & float_flag_invalid) {
            hw_exc |= EXC_M_INV;
        }
        if (exc & float_flag_divbyzero) {
            hw_exc |= EXC_M_DZE;
        }
        if (exc & float_flag_overflow) {
            hw_exc |= EXC_M_FOV;
        }
        if (exc & float_flag_underflow) {
            hw_exc |= EXC_M_UNF;
        }
        if (exc & float_flag_inexact) {
            hw_exc |= EXC_M_INE;
        }

        arith_excp(env, retaddr, hw_exc, 1ull << regno);
    }
}

/* Raise exceptions for ieee fp insns without software completion.
   In that case there are no exceptions that don't trap; the mask
   doesn't apply.  */
void helper_fp_exc_raise(CPUAlphaState *env, uint32_t exc, uint32_t regno)
{
    inline_fp_exc_raise(env, GETPC(), exc, regno);
}

/* Raise exceptions for ieee fp insns with software completion.  */
void helper_fp_exc_raise_s(CPUAlphaState *env, uint32_t exc, uint32_t regno)
{
    if (exc) {
        env->fpcr_exc_status |= exc;
        exc &= ~env->fpcr_exc_mask;
        inline_fp_exc_raise(env, GETPC(), exc, regno);
    }
}

/* Input handing without software completion.  Trap for all
   non-finite numbers.  */
void helper_ieee_input(CPUAlphaState *env, uint64_t val)
{
    uint32_t exp = (uint32_t)(val >> 52) & 0x7ff;
    uint64_t frac = val & 0xfffffffffffffull;

    if (exp == 0) {
        /* Denormals without DNZ set raise an exception.  */
        if (frac != 0 && !env->fp_status.flush_inputs_to_zero) {
            arith_excp(env, GETPC(), EXC_M_UNF, 0);
        }
    } else if (exp == 0x7ff) {
        /* Infinity or NaN.  */
        /* ??? I'm not sure these exception bit flags are correct.  I do
           know that the Linux kernel, at least, doesn't rely on them and
           just emulates the insn to figure out what exception to use.  */
        arith_excp(env, GETPC(), frac ? EXC_M_INV : EXC_M_FOV, 0);
    }
}

/* Similar, but does not trap for infinities.  Used for comparisons.  */
void helper_ieee_input_cmp(CPUAlphaState *env, uint64_t val)
{
    uint32_t exp = (uint32_t)(val >> 52) & 0x7ff;
    uint64_t frac = val & 0xfffffffffffffull;

    if (exp == 0) {
        /* Denormals without DNZ set raise an exception.  */
        if (frac != 0 && !env->fp_status.flush_inputs_to_zero) {
            arith_excp(env, GETPC(), EXC_M_UNF, 0);
        }
    } else if (exp == 0x7ff && frac) {
        /* NaN.  */
        arith_excp(env, GETPC(), EXC_M_INV, 0);
    }
}

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


/* S floating (single) */

/* Taken from linux/arch/alpha/kernel/traps.c, s_mem_to_reg.  */
static inline uint64_t float32_to_s_int(uint32_t fi)
{
    uint32_t frac = fi & 0x7fffff;
    uint32_t sign = fi >> 31;
    uint32_t exp_msb = (fi >> 30) & 1;
    uint32_t exp_low = (fi >> 23) & 0x7f;
    uint32_t exp;

    exp = (exp_msb << 10) | exp_low;
    if (exp_msb) {
        if (exp_low == 0x7f) {
            exp = 0x7ff;
        }
    } else {
        if (exp_low != 0x00) {
            exp |= 0x380;
        }
    }

    return (((uint64_t)sign << 63)
            | ((uint64_t)exp << 52)
            | ((uint64_t)frac << 29));
}

static inline uint64_t float32_to_s(float32 fa)
{
    CPU_FloatU a;
    a.f = fa;
    return float32_to_s_int(a.l);
}

static inline uint32_t s_to_float32_int(uint64_t a)
{
    return ((a >> 32) & 0xc0000000) | ((a >> 29) & 0x3fffffff);
}

static inline float32 s_to_float32(uint64_t a)
{
    CPU_FloatU r;
    r.l = s_to_float32_int(a);
    return r.f;
}

uint32_t helper_s_to_memory(uint64_t a)
{
    return s_to_float32_int(a);
}

uint64_t helper_memory_to_s(uint32_t a)
{
    return float32_to_s_int(a);
}

uint64_t helper_adds(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = s_to_float32(a);
    fb = s_to_float32(b);
    fr = float32_add(fa, fb, &FP_STATUS);
    return float32_to_s(fr);
}

uint64_t helper_subs(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = s_to_float32(a);
    fb = s_to_float32(b);
    fr = float32_sub(fa, fb, &FP_STATUS);
    return float32_to_s(fr);
}

uint64_t helper_muls(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = s_to_float32(a);
    fb = s_to_float32(b);
    fr = float32_mul(fa, fb, &FP_STATUS);
    return float32_to_s(fr);
}

uint64_t helper_divs(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = s_to_float32(a);
    fb = s_to_float32(b);
    fr = float32_div(fa, fb, &FP_STATUS);
    return float32_to_s(fr);
}

uint64_t helper_sqrts(CPUAlphaState *env, uint64_t a)
{
    float32 fa, fr;

    fa = s_to_float32(a);
    fr = float32_sqrt(fa, &FP_STATUS);
    return float32_to_s(fr);
}


/* T floating (double) */
static inline float64 t_to_float64(uint64_t a)
{
    /* Memory format is the same as float64 */
    CPU_DoubleU r;
    r.ll = a;
    return r.d;
}

static inline uint64_t float64_to_t(float64 fa)
{
    /* Memory format is the same as float64 */
    CPU_DoubleU r;
    r.d = fa;
    return r.ll;
}

uint64_t helper_addt(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = t_to_float64(a);
    fb = t_to_float64(b);
    fr = float64_add(fa, fb, &FP_STATUS);
    return float64_to_t(fr);
}

uint64_t helper_subt(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = t_to_float64(a);
    fb = t_to_float64(b);
    fr = float64_sub(fa, fb, &FP_STATUS);
    return float64_to_t(fr);
}

uint64_t helper_mult(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = t_to_float64(a);
    fb = t_to_float64(b);
    fr = float64_mul(fa, fb, &FP_STATUS);
    return float64_to_t(fr);
}

uint64_t helper_divt(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = t_to_float64(a);
    fb = t_to_float64(b);
    fr = float64_div(fa, fb, &FP_STATUS);
    return float64_to_t(fr);
}

uint64_t helper_sqrtt(CPUAlphaState *env, uint64_t a)
{
    float64 fa, fr;

    fa = t_to_float64(a);
    fr = float64_sqrt(fa, &FP_STATUS);
    return float64_to_t(fr);
}

/* Comparisons */
uint64_t helper_cmptun(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = t_to_float64(a);
    fb = t_to_float64(b);

    if (float64_unordered_quiet(fa, fb, &FP_STATUS)) {
        return 0x4000000000000000ULL;
    } else {
        return 0;
    }
}

uint64_t helper_cmpteq(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = t_to_float64(a);
    fb = t_to_float64(b);

    if (float64_eq_quiet(fa, fb, &FP_STATUS)) {
        return 0x4000000000000000ULL;
    } else {
        return 0;
    }
}

uint64_t helper_cmptle(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = t_to_float64(a);
    fb = t_to_float64(b);

    if (float64_le(fa, fb, &FP_STATUS)) {
        return 0x4000000000000000ULL;
    } else {
        return 0;
    }
}

uint64_t helper_cmptlt(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = t_to_float64(a);
    fb = t_to_float64(b);

    if (float64_lt(fa, fb, &FP_STATUS)) {
        return 0x4000000000000000ULL;
    } else {
        return 0;
    }
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

/* Floating point format conversion */
uint64_t helper_cvtts(CPUAlphaState *env, uint64_t a)
{
    float64 fa;
    float32 fr;

    fa = t_to_float64(a);
    fr = float64_to_float32(fa, &FP_STATUS);
    return float32_to_s(fr);
}

uint64_t helper_cvtst(CPUAlphaState *env, uint64_t a)
{
    float32 fa;
    float64 fr;

    fa = s_to_float32(a);
    fr = float32_to_float64(fa, &FP_STATUS);
    return float64_to_t(fr);
}

uint64_t helper_cvtqs(CPUAlphaState *env, uint64_t a)
{
    float32 fr = int64_to_float32(a, &FP_STATUS);
    return float32_to_s(fr);
}

/* Implement float64 to uint64 conversion without saturation -- we must
   supply the truncated result.  This behaviour is used by the compiler
   to get unsigned conversion for free with the same instruction.

   The VI flag is set when overflow or inexact exceptions should be raised.  */

static inline uint64_t inline_cvttq(CPUAlphaState *env, uint64_t a,
                                    int roundmode, int VI)
{
    uint64_t frac, ret = 0;
    uint32_t exp, sign, exc = 0;
    int shift;

    sign = (a >> 63);
    exp = (uint32_t)(a >> 52) & 0x7ff;
    frac = a & 0xfffffffffffffull;

    if (exp == 0) {
        if (unlikely(frac != 0)) {
            goto do_underflow;
        }
    } else if (exp == 0x7ff) {
        exc = (frac ? float_flag_invalid : VI ? float_flag_overflow : 0);
    } else {
        /* Restore implicit bit.  */
        frac |= 0x10000000000000ull;

        shift = exp - 1023 - 52;
        if (shift >= 0) {
            /* In this case the number is so large that we must shift
               the fraction left.  There is no rounding to do.  */
            if (shift < 63) {
                ret = frac << shift;
                if (VI && (ret >> shift) != frac) {
                    exc = float_flag_overflow;
                }
            }
        } else {
            uint64_t round;

            /* In this case the number is smaller than the fraction as
               represented by the 52 bit number.  Here we must think
               about rounding the result.  Handle this by shifting the
               fractional part of the number into the high bits of ROUND.
               This will let us efficiently handle round-to-nearest.  */
            shift = -shift;
            if (shift < 63) {
                ret = frac >> shift;
                round = frac << (64 - shift);
            } else {
                /* The exponent is so small we shift out everything.
                   Leave a sticky bit for proper rounding below.  */
            do_underflow:
                round = 1;
            }

            if (round) {
                exc = (VI ? float_flag_inexact : 0);
                switch (roundmode) {
                case float_round_nearest_even:
                    if (round == (1ull << 63)) {
                        /* Fraction is exactly 0.5; round to even.  */
                        ret += (ret & 1);
                    } else if (round > (1ull << 63)) {
                        ret += 1;
                    }
                    break;
                case float_round_to_zero:
                    break;
                case float_round_up:
                    ret += 1 - sign;
                    break;
                case float_round_down:
                    ret += sign;
                    break;
                }
            }
        }
        if (sign) {
            ret = -ret;
        }
    }
    if (unlikely(exc)) {
        float_raise(exc, &FP_STATUS);
    }

    return ret;
}

uint64_t helper_cvttq(CPUAlphaState *env, uint64_t a)
{
    return inline_cvttq(env, a, FP_STATUS.float_rounding_mode, 1);
}

uint64_t helper_cvttq_c(CPUAlphaState *env, uint64_t a)
{
    return inline_cvttq(env, a, float_round_to_zero, 0);
}

uint64_t helper_cvttq_svic(CPUAlphaState *env, uint64_t a)
{
    return inline_cvttq(env, a, float_round_to_zero, 1);
}

uint64_t helper_cvtqt(CPUAlphaState *env, uint64_t a)
{
    float64 fr = int64_to_float64(a, &FP_STATUS);
    return float64_to_t(fr);
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
