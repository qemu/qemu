/*
 *  Helpers for floating point instructions.
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


void helper_setroundmode(CPUAlphaState *env, uint32_t val)
{
    set_float_rounding_mode(val, &FP_STATUS);
}

void helper_setflushzero(CPUAlphaState *env, uint32_t val)
{
    set_flush_to_zero(val, &FP_STATUS);
}

#define CONVERT_BIT(X, SRC, DST) \
    (SRC > DST ? (X) / (SRC / DST) & (DST) : ((X) & SRC) * (DST / SRC))

static uint32_t soft_to_fpcr_exc(CPUAlphaState *env)
{
    uint8_t exc = get_float_exception_flags(&FP_STATUS);
    uint32_t ret = 0;

    if (unlikely(exc)) {
        set_float_exception_flags(0, &FP_STATUS);
        ret |= CONVERT_BIT(exc, float_flag_invalid, FPCR_INV);
        ret |= CONVERT_BIT(exc, float_flag_divbyzero, FPCR_DZE);
        ret |= CONVERT_BIT(exc, float_flag_overflow, FPCR_OVF);
        ret |= CONVERT_BIT(exc, float_flag_underflow, FPCR_UNF);
        ret |= CONVERT_BIT(exc, float_flag_inexact, FPCR_INE);
    }

    return ret;
}

static void fp_exc_raise1(CPUAlphaState *env, uintptr_t retaddr,
                          uint32_t exc, uint32_t regno, uint32_t hw_exc)
{
    hw_exc |= CONVERT_BIT(exc, FPCR_INV, EXC_M_INV);
    hw_exc |= CONVERT_BIT(exc, FPCR_DZE, EXC_M_DZE);
    hw_exc |= CONVERT_BIT(exc, FPCR_OVF, EXC_M_FOV);
    hw_exc |= CONVERT_BIT(exc, FPCR_UNF, EXC_M_UNF);
    hw_exc |= CONVERT_BIT(exc, FPCR_INE, EXC_M_INE);
    hw_exc |= CONVERT_BIT(exc, FPCR_IOV, EXC_M_IOV);

    arith_excp(env, retaddr, hw_exc, 1ull << regno);
}

/* Raise exceptions for ieee fp insns without software completion.
   In that case there are no exceptions that don't trap; the mask
   doesn't apply.  */
void helper_fp_exc_raise(CPUAlphaState *env, uint32_t ignore, uint32_t regno)
{
    uint32_t exc = env->error_code;
    if (exc) {
        env->fpcr |= exc;
        exc &= ~ignore;
        if (exc) {
            fp_exc_raise1(env, GETPC(), exc, regno, 0);
        }
    }
}

/* Raise exceptions for ieee fp insns with software completion.  */
void helper_fp_exc_raise_s(CPUAlphaState *env, uint32_t ignore, uint32_t regno)
{
    uint32_t exc = env->error_code & ~ignore;
    if (exc) {
        env->fpcr |= exc;
        exc &= env->fpcr_exc_enable;
        /*
         * In system mode, the software handler gets invoked
         * for any non-ignored exception.
         * In user mode, the kernel's software handler only
         * delivers a signal if the exception is enabled.
         */
#ifdef CONFIG_USER_ONLY
        if (!exc) {
            return;
        }
#endif
        fp_exc_raise1(env, GETPC(), exc, regno, EXC_M_SWC);
    }
}

/* Input handing without software completion.  Trap for all
   non-finite numbers.  */
void helper_ieee_input(CPUAlphaState *env, uint64_t val)
{
    uint32_t exp = (uint32_t)(val >> 52) & 0x7ff;
    uint64_t frac = val & 0xfffffffffffffull;

    if (exp == 0) {
        /* Denormals without /S raise an exception.  */
        if (frac != 0) {
            arith_excp(env, GETPC(), EXC_M_INV, 0);
        }
    } else if (exp == 0x7ff) {
        /* Infinity or NaN.  */
        env->fpcr |= FPCR_INV;
        arith_excp(env, GETPC(), EXC_M_INV, 0);
    }
}

/* Similar, but does not trap for infinities.  Used for comparisons.  */
void helper_ieee_input_cmp(CPUAlphaState *env, uint64_t val)
{
    uint32_t exp = (uint32_t)(val >> 52) & 0x7ff;
    uint64_t frac = val & 0xfffffffffffffull;

    if (exp == 0) {
        /* Denormals without /S raise an exception.  */
        if (frac != 0) {
            arith_excp(env, GETPC(), EXC_M_INV, 0);
        }
    } else if (exp == 0x7ff && frac) {
        /* NaN.  */
        env->fpcr |= FPCR_INV;
        arith_excp(env, GETPC(), EXC_M_INV, 0);
    }
}

/* Input handing with software completion.  Trap for denorms, unless DNZ
   is set.  If we try to support DNOD (which none of the produced hardware
   did, AFAICS), we'll need to suppress the trap when FPCR.DNOD is set;
   then the code downstream of that will need to cope with denorms sans
   flush_input_to_zero.  Most of it should work sanely, but there's
   nothing to compare with.  */
void helper_ieee_input_s(CPUAlphaState *env, uint64_t val)
{
    if (unlikely(2 * val - 1 < 0x1fffffffffffffull)
        && !env->fp_status.flush_inputs_to_zero) {
        arith_excp(env, GETPC(), EXC_M_INV | EXC_M_SWC, 0);
    }
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
    env->error_code = soft_to_fpcr_exc(env);

    return float32_to_s(fr);
}

uint64_t helper_subs(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = s_to_float32(a);
    fb = s_to_float32(b);
    fr = float32_sub(fa, fb, &FP_STATUS);
    env->error_code = soft_to_fpcr_exc(env);

    return float32_to_s(fr);
}

uint64_t helper_muls(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = s_to_float32(a);
    fb = s_to_float32(b);
    fr = float32_mul(fa, fb, &FP_STATUS);
    env->error_code = soft_to_fpcr_exc(env);

    return float32_to_s(fr);
}

uint64_t helper_divs(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = s_to_float32(a);
    fb = s_to_float32(b);
    fr = float32_div(fa, fb, &FP_STATUS);
    env->error_code = soft_to_fpcr_exc(env);

    return float32_to_s(fr);
}

uint64_t helper_sqrts(CPUAlphaState *env, uint64_t a)
{
    float32 fa, fr;

    fa = s_to_float32(a);
    fr = float32_sqrt(fa, &FP_STATUS);
    env->error_code = soft_to_fpcr_exc(env);

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
    env->error_code = soft_to_fpcr_exc(env);

    return float64_to_t(fr);
}

uint64_t helper_subt(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = t_to_float64(a);
    fb = t_to_float64(b);
    fr = float64_sub(fa, fb, &FP_STATUS);
    env->error_code = soft_to_fpcr_exc(env);

    return float64_to_t(fr);
}

uint64_t helper_mult(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = t_to_float64(a);
    fb = t_to_float64(b);
    fr = float64_mul(fa, fb, &FP_STATUS);
    env->error_code = soft_to_fpcr_exc(env);

    return float64_to_t(fr);
}

uint64_t helper_divt(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = t_to_float64(a);
    fb = t_to_float64(b);
    fr = float64_div(fa, fb, &FP_STATUS);
    env->error_code = soft_to_fpcr_exc(env);

    return float64_to_t(fr);
}

uint64_t helper_sqrtt(CPUAlphaState *env, uint64_t a)
{
    float64 fa, fr;

    fa = t_to_float64(a);
    fr = float64_sqrt(fa, &FP_STATUS);
    env->error_code = soft_to_fpcr_exc(env);

    return float64_to_t(fr);
}

/* Comparisons */
uint64_t helper_cmptun(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb;
    uint64_t ret = 0;

    fa = t_to_float64(a);
    fb = t_to_float64(b);

    if (float64_unordered_quiet(fa, fb, &FP_STATUS)) {
        ret = 0x4000000000000000ULL;
    }
    env->error_code = soft_to_fpcr_exc(env);

    return ret;
}

uint64_t helper_cmpteq(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb;
    uint64_t ret = 0;

    fa = t_to_float64(a);
    fb = t_to_float64(b);

    if (float64_eq_quiet(fa, fb, &FP_STATUS)) {
        ret = 0x4000000000000000ULL;
    }
    env->error_code = soft_to_fpcr_exc(env);

    return ret;
}

uint64_t helper_cmptle(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb;
    uint64_t ret = 0;

    fa = t_to_float64(a);
    fb = t_to_float64(b);

    if (float64_le(fa, fb, &FP_STATUS)) {
        ret = 0x4000000000000000ULL;
    }
    env->error_code = soft_to_fpcr_exc(env);

    return ret;
}

uint64_t helper_cmptlt(CPUAlphaState *env, uint64_t a, uint64_t b)
{
    float64 fa, fb;
    uint64_t ret = 0;

    fa = t_to_float64(a);
    fb = t_to_float64(b);

    if (float64_lt(fa, fb, &FP_STATUS)) {
        ret = 0x4000000000000000ULL;
    }
    env->error_code = soft_to_fpcr_exc(env);

    return ret;
}

/* Floating point format conversion */
uint64_t helper_cvtts(CPUAlphaState *env, uint64_t a)
{
    float64 fa;
    float32 fr;

    fa = t_to_float64(a);
    fr = float64_to_float32(fa, &FP_STATUS);
    env->error_code = soft_to_fpcr_exc(env);

    return float32_to_s(fr);
}

uint64_t helper_cvtst(CPUAlphaState *env, uint64_t a)
{
    float32 fa;
    float64 fr;

    fa = s_to_float32(a);
    fr = float32_to_float64(fa, &FP_STATUS);
    env->error_code = soft_to_fpcr_exc(env);

    return float64_to_t(fr);
}

uint64_t helper_cvtqs(CPUAlphaState *env, uint64_t a)
{
    float32 fr = int64_to_float32(a, &FP_STATUS);
    env->error_code = soft_to_fpcr_exc(env);

    return float32_to_s(fr);
}

/* Implement float64 to uint64_t conversion without saturation -- we must
   supply the truncated result.  This behaviour is used by the compiler
   to get unsigned conversion for free with the same instruction.  */

static uint64_t do_cvttq(CPUAlphaState *env, uint64_t a, int roundmode)
{
    float64 fa;
    int64_t ret;
    uint32_t exc = 0;
    int flags;

    fa = t_to_float64(a);
    ret = float64_to_int64_modulo(fa, roundmode, &FP_STATUS);

    flags = get_float_exception_flags(&FP_STATUS);
    if (unlikely(flags)) {
        set_float_exception_flags(0, &FP_STATUS);

        /* We need to massage the resulting exceptions. */
        if (flags & float_flag_invalid_cvti) {
            /* Overflow, either normal or infinity. */
            if (float64_is_infinity(fa)) {
                exc = FPCR_INV;
            } else {
                exc = FPCR_IOV | FPCR_INE;
            }
        } else if (flags & float_flag_invalid) {
            exc = FPCR_INV;
        } else if (flags & float_flag_inexact) {
            exc = FPCR_INE;
        }
    }
    env->error_code = exc;

    return ret;
}

uint64_t helper_cvttq(CPUAlphaState *env, uint64_t a)
{
    return do_cvttq(env, a, FP_STATUS.float_rounding_mode);
}

uint64_t helper_cvttq_c(CPUAlphaState *env, uint64_t a)
{
    return do_cvttq(env, a, float_round_to_zero);
}

uint64_t helper_cvtqt(CPUAlphaState *env, uint64_t a)
{
    float64 fr = int64_to_float64(a, &FP_STATUS);
    env->error_code = soft_to_fpcr_exc(env);
    return float64_to_t(fr);
}

uint64_t helper_cvtql(CPUAlphaState *env, uint64_t val)
{
    uint32_t exc = 0;
    if (val != (int32_t)val) {
        exc = FPCR_IOV | FPCR_INE;
    }
    env->error_code = exc;

    return ((val & 0xc0000000) << 32) | ((val & 0x3fffffff) << 29);
}
