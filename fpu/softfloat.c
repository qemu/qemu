/*
 * QEMU float support
 *
 * The code in this source file is derived from release 2a of the SoftFloat
 * IEC/IEEE Floating-point Arithmetic Package. Those parts of the code (and
 * some later contributions) are provided under that license, as detailed below.
 * It has subsequently been modified by contributors to the QEMU Project,
 * so some portions are provided under:
 *  the SoftFloat-2a license
 *  the BSD license
 *  GPL-v2-or-later
 *
 * Any future contributions to this file after December 1st 2014 will be
 * taken to be licensed under the Softfloat-2a license unless specifically
 * indicated otherwise.
 */

/*
===============================================================================
This C source file is part of the SoftFloat IEC/IEEE Floating-point
Arithmetic Package, Release 2a.

Written by John R. Hauser.  This work was made possible in part by the
International Computer Science Institute, located at Suite 600, 1947 Center
Street, Berkeley, California 94704.  Funding was partially provided by the
National Science Foundation under grant MIP-9311980.  The original version
of this code was written as part of a project to build a fixed-point vector
processor in collaboration with the University of California at Berkeley,
overseen by Profs. Nelson Morgan and John Wawrzynek.  More information
is available through the Web page `http://HTTP.CS.Berkeley.EDU/~jhauser/
arithmetic/SoftFloat.html'.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort
has been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT
TIMES RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO
PERSONS AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ANY
AND ALL LOSSES, COSTS, OR OTHER PROBLEMS ARISING FROM ITS USE.

Derivative works are acceptable, even for commercial purposes, so long as
(1) they include prominent notice that the work is derivative, and (2) they
include prominent notice akin to these four paragraphs for those parts of
this code that are retained.

===============================================================================
*/

/* BSD licensing:
 * Copyright (c) 2006, Fabrice Bellard
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Portions of this work are licensed under the terms of the GNU GPL,
 * version 2 or later. See the COPYING file in the top-level directory.
 */

/* softfloat (and in particular the code in softfloat-specialize.h) is
 * target-dependent and needs the TARGET_* macros.
 */
#include "qemu/osdep.h"
#include <math.h>
#include "qemu/bitops.h"
#include "fpu/softfloat.h"

/* We only need stdlib for abort() */

/*----------------------------------------------------------------------------
| Primitive arithmetic functions, including multi-word arithmetic, and
| division and square root approximations.  (Can be specialized to target if
| desired.)
*----------------------------------------------------------------------------*/
#include "fpu/softfloat-macros.h"

/*
 * Hardfloat
 *
 * Fast emulation of guest FP instructions is challenging for two reasons.
 * First, FP instruction semantics are similar but not identical, particularly
 * when handling NaNs. Second, emulating at reasonable speed the guest FP
 * exception flags is not trivial: reading the host's flags register with a
 * feclearexcept & fetestexcept pair is slow [slightly slower than soft-fp],
 * and trapping on every FP exception is not fast nor pleasant to work with.
 *
 * We address these challenges by leveraging the host FPU for a subset of the
 * operations. To do this we expand on the idea presented in this paper:
 *
 * Guo, Yu-Chuan, et al. "Translating the ARM Neon and VFP instructions in a
 * binary translator." Software: Practice and Experience 46.12 (2016):1591-1615.
 *
 * The idea is thus to leverage the host FPU to (1) compute FP operations
 * and (2) identify whether FP exceptions occurred while avoiding
 * expensive exception flag register accesses.
 *
 * An important optimization shown in the paper is that given that exception
 * flags are rarely cleared by the guest, we can avoid recomputing some flags.
 * This is particularly useful for the inexact flag, which is very frequently
 * raised in floating-point workloads.
 *
 * We optimize the code further by deferring to soft-fp whenever FP exception
 * detection might get hairy. Two examples: (1) when at least one operand is
 * denormal/inf/NaN; (2) when operands are not guaranteed to lead to a 0 result
 * and the result is < the minimum normal.
 */
#define GEN_INPUT_FLUSH__NOCHECK(name, soft_t)                          \
    static inline void name(soft_t *a, float_status *s)                 \
    {                                                                   \
        if (unlikely(soft_t ## _is_denormal(*a))) {                     \
            *a = soft_t ## _set_sign(soft_t ## _zero,                   \
                                     soft_t ## _is_neg(*a));            \
            s->float_exception_flags |= float_flag_input_denormal;      \
        }                                                               \
    }

GEN_INPUT_FLUSH__NOCHECK(float32_input_flush__nocheck, float32)
GEN_INPUT_FLUSH__NOCHECK(float64_input_flush__nocheck, float64)
#undef GEN_INPUT_FLUSH__NOCHECK

#define GEN_INPUT_FLUSH1(name, soft_t)                  \
    static inline void name(soft_t *a, float_status *s) \
    {                                                   \
        if (likely(!s->flush_inputs_to_zero)) {         \
            return;                                     \
        }                                               \
        soft_t ## _input_flush__nocheck(a, s);          \
    }

GEN_INPUT_FLUSH1(float32_input_flush1, float32)
GEN_INPUT_FLUSH1(float64_input_flush1, float64)
#undef GEN_INPUT_FLUSH1

#define GEN_INPUT_FLUSH2(name, soft_t)                                  \
    static inline void name(soft_t *a, soft_t *b, float_status *s)      \
    {                                                                   \
        if (likely(!s->flush_inputs_to_zero)) {                         \
            return;                                                     \
        }                                                               \
        soft_t ## _input_flush__nocheck(a, s);                          \
        soft_t ## _input_flush__nocheck(b, s);                          \
    }

GEN_INPUT_FLUSH2(float32_input_flush2, float32)
GEN_INPUT_FLUSH2(float64_input_flush2, float64)
#undef GEN_INPUT_FLUSH2

#define GEN_INPUT_FLUSH3(name, soft_t)                                  \
    static inline void name(soft_t *a, soft_t *b, soft_t *c, float_status *s) \
    {                                                                   \
        if (likely(!s->flush_inputs_to_zero)) {                         \
            return;                                                     \
        }                                                               \
        soft_t ## _input_flush__nocheck(a, s);                          \
        soft_t ## _input_flush__nocheck(b, s);                          \
        soft_t ## _input_flush__nocheck(c, s);                          \
    }

GEN_INPUT_FLUSH3(float32_input_flush3, float32)
GEN_INPUT_FLUSH3(float64_input_flush3, float64)
#undef GEN_INPUT_FLUSH3

/*
 * Choose whether to use fpclassify or float32/64_* primitives in the generated
 * hardfloat functions. Each combination of number of inputs and float size
 * gets its own value.
 */
#if defined(__x86_64__)
# define QEMU_HARDFLOAT_1F32_USE_FP 0
# define QEMU_HARDFLOAT_1F64_USE_FP 1
# define QEMU_HARDFLOAT_2F32_USE_FP 0
# define QEMU_HARDFLOAT_2F64_USE_FP 1
# define QEMU_HARDFLOAT_3F32_USE_FP 0
# define QEMU_HARDFLOAT_3F64_USE_FP 1
#else
# define QEMU_HARDFLOAT_1F32_USE_FP 0
# define QEMU_HARDFLOAT_1F64_USE_FP 0
# define QEMU_HARDFLOAT_2F32_USE_FP 0
# define QEMU_HARDFLOAT_2F64_USE_FP 0
# define QEMU_HARDFLOAT_3F32_USE_FP 0
# define QEMU_HARDFLOAT_3F64_USE_FP 0
#endif

/*
 * QEMU_HARDFLOAT_USE_ISINF chooses whether to use isinf() over
 * float{32,64}_is_infinity when !USE_FP.
 * On x86_64/aarch64, using the former over the latter can yield a ~6% speedup.
 * On power64 however, using isinf() reduces fp-bench performance by up to 50%.
 */
#if defined(__x86_64__) || defined(__aarch64__)
# define QEMU_HARDFLOAT_USE_ISINF   1
#else
# define QEMU_HARDFLOAT_USE_ISINF   0
#endif

/*
 * Some targets clear the FP flags before most FP operations. This prevents
 * the use of hardfloat, since hardfloat relies on the inexact flag being
 * already set.
 */
#if defined(TARGET_PPC) || defined(__FAST_MATH__)
# if defined(__FAST_MATH__)
#  warning disabling hardfloat due to -ffast-math: hardfloat requires an exact \
    IEEE implementation
# endif
# define QEMU_NO_HARDFLOAT 1
# define QEMU_SOFTFLOAT_ATTR QEMU_FLATTEN
#else
# define QEMU_NO_HARDFLOAT 0
# define QEMU_SOFTFLOAT_ATTR QEMU_FLATTEN __attribute__((noinline))
#endif

static inline bool can_use_fpu(const float_status *s)
{
    if (QEMU_NO_HARDFLOAT) {
        return false;
    }
    return likely(s->float_exception_flags & float_flag_inexact &&
                  s->float_rounding_mode == float_round_nearest_even);
}

/*
 * Hardfloat generation functions. Each operation can have two flavors:
 * either using softfloat primitives (e.g. float32_is_zero_or_normal) for
 * most condition checks, or native ones (e.g. fpclassify).
 *
 * The flavor is chosen by the callers. Instead of using macros, we rely on the
 * compiler to propagate constants and inline everything into the callers.
 *
 * We only generate functions for operations with two inputs, since only
 * these are common enough to justify consolidating them into common code.
 */

typedef union {
    float32 s;
    float h;
} union_float32;

typedef union {
    float64 s;
    double h;
} union_float64;

typedef bool (*f32_check_fn)(union_float32 a, union_float32 b);
typedef bool (*f64_check_fn)(union_float64 a, union_float64 b);

typedef float32 (*soft_f32_op2_fn)(float32 a, float32 b, float_status *s);
typedef float64 (*soft_f64_op2_fn)(float64 a, float64 b, float_status *s);
typedef float   (*hard_f32_op2_fn)(float a, float b);
typedef double  (*hard_f64_op2_fn)(double a, double b);

/* 2-input is-zero-or-normal */
static inline bool f32_is_zon2(union_float32 a, union_float32 b)
{
    if (QEMU_HARDFLOAT_2F32_USE_FP) {
        /*
         * Not using a temp variable for consecutive fpclassify calls ends up
         * generating faster code.
         */
        return (fpclassify(a.h) == FP_NORMAL || fpclassify(a.h) == FP_ZERO) &&
               (fpclassify(b.h) == FP_NORMAL || fpclassify(b.h) == FP_ZERO);
    }
    return float32_is_zero_or_normal(a.s) &&
           float32_is_zero_or_normal(b.s);
}

static inline bool f64_is_zon2(union_float64 a, union_float64 b)
{
    if (QEMU_HARDFLOAT_2F64_USE_FP) {
        return (fpclassify(a.h) == FP_NORMAL || fpclassify(a.h) == FP_ZERO) &&
               (fpclassify(b.h) == FP_NORMAL || fpclassify(b.h) == FP_ZERO);
    }
    return float64_is_zero_or_normal(a.s) &&
           float64_is_zero_or_normal(b.s);
}

/* 3-input is-zero-or-normal */
static inline
bool f32_is_zon3(union_float32 a, union_float32 b, union_float32 c)
{
    if (QEMU_HARDFLOAT_3F32_USE_FP) {
        return (fpclassify(a.h) == FP_NORMAL || fpclassify(a.h) == FP_ZERO) &&
               (fpclassify(b.h) == FP_NORMAL || fpclassify(b.h) == FP_ZERO) &&
               (fpclassify(c.h) == FP_NORMAL || fpclassify(c.h) == FP_ZERO);
    }
    return float32_is_zero_or_normal(a.s) &&
           float32_is_zero_or_normal(b.s) &&
           float32_is_zero_or_normal(c.s);
}

static inline
bool f64_is_zon3(union_float64 a, union_float64 b, union_float64 c)
{
    if (QEMU_HARDFLOAT_3F64_USE_FP) {
        return (fpclassify(a.h) == FP_NORMAL || fpclassify(a.h) == FP_ZERO) &&
               (fpclassify(b.h) == FP_NORMAL || fpclassify(b.h) == FP_ZERO) &&
               (fpclassify(c.h) == FP_NORMAL || fpclassify(c.h) == FP_ZERO);
    }
    return float64_is_zero_or_normal(a.s) &&
           float64_is_zero_or_normal(b.s) &&
           float64_is_zero_or_normal(c.s);
}

static inline bool f32_is_inf(union_float32 a)
{
    if (QEMU_HARDFLOAT_USE_ISINF) {
        return isinf(a.h);
    }
    return float32_is_infinity(a.s);
}

static inline bool f64_is_inf(union_float64 a)
{
    if (QEMU_HARDFLOAT_USE_ISINF) {
        return isinf(a.h);
    }
    return float64_is_infinity(a.s);
}

/* Note: @fast_test and @post can be NULL */
static inline float32
float32_gen2(float32 xa, float32 xb, float_status *s,
             hard_f32_op2_fn hard, soft_f32_op2_fn soft,
             f32_check_fn pre, f32_check_fn post,
             f32_check_fn fast_test, soft_f32_op2_fn fast_op)
{
    union_float32 ua, ub, ur;

    ua.s = xa;
    ub.s = xb;

    if (unlikely(!can_use_fpu(s))) {
        goto soft;
    }

    float32_input_flush2(&ua.s, &ub.s, s);
    if (unlikely(!pre(ua, ub))) {
        goto soft;
    }
    if (fast_test && fast_test(ua, ub)) {
        return fast_op(ua.s, ub.s, s);
    }

    ur.h = hard(ua.h, ub.h);
    if (unlikely(f32_is_inf(ur))) {
        s->float_exception_flags |= float_flag_overflow;
    } else if (unlikely(fabsf(ur.h) <= FLT_MIN)) {
        if (post == NULL || post(ua, ub)) {
            goto soft;
        }
    }
    return ur.s;

 soft:
    return soft(ua.s, ub.s, s);
}

static inline float64
float64_gen2(float64 xa, float64 xb, float_status *s,
             hard_f64_op2_fn hard, soft_f64_op2_fn soft,
             f64_check_fn pre, f64_check_fn post,
             f64_check_fn fast_test, soft_f64_op2_fn fast_op)
{
    union_float64 ua, ub, ur;

    ua.s = xa;
    ub.s = xb;

    if (unlikely(!can_use_fpu(s))) {
        goto soft;
    }

    float64_input_flush2(&ua.s, &ub.s, s);
    if (unlikely(!pre(ua, ub))) {
        goto soft;
    }
    if (fast_test && fast_test(ua, ub)) {
        return fast_op(ua.s, ub.s, s);
    }

    ur.h = hard(ua.h, ub.h);
    if (unlikely(f64_is_inf(ur))) {
        s->float_exception_flags |= float_flag_overflow;
    } else if (unlikely(fabs(ur.h) <= DBL_MIN)) {
        if (post == NULL || post(ua, ub)) {
            goto soft;
        }
    }
    return ur.s;

 soft:
    return soft(ua.s, ub.s, s);
}

/*----------------------------------------------------------------------------
| Returns the fraction bits of the single-precision floating-point value `a'.
*----------------------------------------------------------------------------*/

static inline uint32_t extractFloat32Frac(float32 a)
{
    return float32_val(a) & 0x007FFFFF;
}

/*----------------------------------------------------------------------------
| Returns the exponent bits of the single-precision floating-point value `a'.
*----------------------------------------------------------------------------*/

static inline int extractFloat32Exp(float32 a)
{
    return (float32_val(a) >> 23) & 0xFF;
}

/*----------------------------------------------------------------------------
| Returns the sign bit of the single-precision floating-point value `a'.
*----------------------------------------------------------------------------*/

static inline flag extractFloat32Sign(float32 a)
{
    return float32_val(a) >> 31;
}

/*----------------------------------------------------------------------------
| Returns the fraction bits of the double-precision floating-point value `a'.
*----------------------------------------------------------------------------*/

static inline uint64_t extractFloat64Frac(float64 a)
{
    return float64_val(a) & UINT64_C(0x000FFFFFFFFFFFFF);
}

/*----------------------------------------------------------------------------
| Returns the exponent bits of the double-precision floating-point value `a'.
*----------------------------------------------------------------------------*/

static inline int extractFloat64Exp(float64 a)
{
    return (float64_val(a) >> 52) & 0x7FF;
}

/*----------------------------------------------------------------------------
| Returns the sign bit of the double-precision floating-point value `a'.
*----------------------------------------------------------------------------*/

static inline flag extractFloat64Sign(float64 a)
{
    return float64_val(a) >> 63;
}

/*
 * Classify a floating point number. Everything above float_class_qnan
 * is a NaN so cls >= float_class_qnan is any NaN.
 */

typedef enum __attribute__ ((__packed__)) {
    float_class_unclassified,
    float_class_zero,
    float_class_normal,
    float_class_inf,
    float_class_qnan,  /* all NaNs from here */
    float_class_snan,
} FloatClass;

/* Simple helpers for checking if, or what kind of, NaN we have */
static inline __attribute__((unused)) bool is_nan(FloatClass c)
{
    return unlikely(c >= float_class_qnan);
}

static inline __attribute__((unused)) bool is_snan(FloatClass c)
{
    return c == float_class_snan;
}

static inline __attribute__((unused)) bool is_qnan(FloatClass c)
{
    return c == float_class_qnan;
}

/*
 * Structure holding all of the decomposed parts of a float. The
 * exponent is unbiased and the fraction is normalized. All
 * calculations are done with a 64 bit fraction and then rounded as
 * appropriate for the final format.
 *
 * Thanks to the packed FloatClass a decent compiler should be able to
 * fit the whole structure into registers and avoid using the stack
 * for parameter passing.
 */

typedef struct {
    uint64_t frac;
    int32_t  exp;
    FloatClass cls;
    bool sign;
} FloatParts;

#define DECOMPOSED_BINARY_POINT    (64 - 2)
#define DECOMPOSED_IMPLICIT_BIT    (1ull << DECOMPOSED_BINARY_POINT)
#define DECOMPOSED_OVERFLOW_BIT    (DECOMPOSED_IMPLICIT_BIT << 1)

/* Structure holding all of the relevant parameters for a format.
 *   exp_size: the size of the exponent field
 *   exp_bias: the offset applied to the exponent field
 *   exp_max: the maximum normalised exponent
 *   frac_size: the size of the fraction field
 *   frac_shift: shift to normalise the fraction with DECOMPOSED_BINARY_POINT
 * The following are computed based the size of fraction
 *   frac_lsb: least significant bit of fraction
 *   frac_lsbm1: the bit below the least significant bit (for rounding)
 *   round_mask/roundeven_mask: masks used for rounding
 * The following optional modifiers are available:
 *   arm_althp: handle ARM Alternative Half Precision
 */
typedef struct {
    int exp_size;
    int exp_bias;
    int exp_max;
    int frac_size;
    int frac_shift;
    uint64_t frac_lsb;
    uint64_t frac_lsbm1;
    uint64_t round_mask;
    uint64_t roundeven_mask;
    bool arm_althp;
} FloatFmt;

/* Expand fields based on the size of exponent and fraction */
#define FLOAT_PARAMS(E, F)                                           \
    .exp_size       = E,                                             \
    .exp_bias       = ((1 << E) - 1) >> 1,                           \
    .exp_max        = (1 << E) - 1,                                  \
    .frac_size      = F,                                             \
    .frac_shift     = DECOMPOSED_BINARY_POINT - F,                   \
    .frac_lsb       = 1ull << (DECOMPOSED_BINARY_POINT - F),         \
    .frac_lsbm1     = 1ull << ((DECOMPOSED_BINARY_POINT - F) - 1),   \
    .round_mask     = (1ull << (DECOMPOSED_BINARY_POINT - F)) - 1,   \
    .roundeven_mask = (2ull << (DECOMPOSED_BINARY_POINT - F)) - 1

static const FloatFmt float16_params = {
    FLOAT_PARAMS(5, 10)
};

static const FloatFmt float16_params_ahp = {
    FLOAT_PARAMS(5, 10),
    .arm_althp = true
};

static const FloatFmt float32_params = {
    FLOAT_PARAMS(8, 23)
};

static const FloatFmt float64_params = {
    FLOAT_PARAMS(11, 52)
};

/* Unpack a float to parts, but do not canonicalize.  */
static inline FloatParts unpack_raw(FloatFmt fmt, uint64_t raw)
{
    const int sign_pos = fmt.frac_size + fmt.exp_size;

    return (FloatParts) {
        .cls = float_class_unclassified,
        .sign = extract64(raw, sign_pos, 1),
        .exp = extract64(raw, fmt.frac_size, fmt.exp_size),
        .frac = extract64(raw, 0, fmt.frac_size),
    };
}

static inline FloatParts float16_unpack_raw(float16 f)
{
    return unpack_raw(float16_params, f);
}

static inline FloatParts float32_unpack_raw(float32 f)
{
    return unpack_raw(float32_params, f);
}

static inline FloatParts float64_unpack_raw(float64 f)
{
    return unpack_raw(float64_params, f);
}

/* Pack a float from parts, but do not canonicalize.  */
static inline uint64_t pack_raw(FloatFmt fmt, FloatParts p)
{
    const int sign_pos = fmt.frac_size + fmt.exp_size;
    uint64_t ret = deposit64(p.frac, fmt.frac_size, fmt.exp_size, p.exp);
    return deposit64(ret, sign_pos, 1, p.sign);
}

static inline float16 float16_pack_raw(FloatParts p)
{
    return make_float16(pack_raw(float16_params, p));
}

static inline float32 float32_pack_raw(FloatParts p)
{
    return make_float32(pack_raw(float32_params, p));
}

static inline float64 float64_pack_raw(FloatParts p)
{
    return make_float64(pack_raw(float64_params, p));
}

/*----------------------------------------------------------------------------
| Functions and definitions to determine:  (1) whether tininess for underflow
| is detected before or after rounding by default, (2) what (if anything)
| happens when exceptions are raised, (3) how signaling NaNs are distinguished
| from quiet NaNs, (4) the default generated quiet NaNs, and (5) how NaNs
| are propagated from function inputs to output.  These details are target-
| specific.
*----------------------------------------------------------------------------*/
#include "softfloat-specialize.inc.c"

/* Canonicalize EXP and FRAC, setting CLS.  */
static FloatParts sf_canonicalize(FloatParts part, const FloatFmt *parm,
                                  float_status *status)
{
    if (part.exp == parm->exp_max && !parm->arm_althp) {
        if (part.frac == 0) {
            part.cls = float_class_inf;
        } else {
            part.frac <<= parm->frac_shift;
            part.cls = (parts_is_snan_frac(part.frac, status)
                        ? float_class_snan : float_class_qnan);
        }
    } else if (part.exp == 0) {
        if (likely(part.frac == 0)) {
            part.cls = float_class_zero;
        } else if (status->flush_inputs_to_zero) {
            float_raise(float_flag_input_denormal, status);
            part.cls = float_class_zero;
            part.frac = 0;
        } else {
            int shift = clz64(part.frac) - 1;
            part.cls = float_class_normal;
            part.exp = parm->frac_shift - parm->exp_bias - shift + 1;
            part.frac <<= shift;
        }
    } else {
        part.cls = float_class_normal;
        part.exp -= parm->exp_bias;
        part.frac = DECOMPOSED_IMPLICIT_BIT + (part.frac << parm->frac_shift);
    }
    return part;
}

/* Round and uncanonicalize a floating-point number by parts. There
 * are FRAC_SHIFT bits that may require rounding at the bottom of the
 * fraction; these bits will be removed. The exponent will be biased
 * by EXP_BIAS and must be bounded by [EXP_MAX-1, 0].
 */

static FloatParts round_canonical(FloatParts p, float_status *s,
                                  const FloatFmt *parm)
{
    const uint64_t frac_lsb = parm->frac_lsb;
    const uint64_t frac_lsbm1 = parm->frac_lsbm1;
    const uint64_t round_mask = parm->round_mask;
    const uint64_t roundeven_mask = parm->roundeven_mask;
    const int exp_max = parm->exp_max;
    const int frac_shift = parm->frac_shift;
    uint64_t frac, inc;
    int exp, flags = 0;
    bool overflow_norm;

    frac = p.frac;
    exp = p.exp;

    switch (p.cls) {
    case float_class_normal:
        switch (s->float_rounding_mode) {
        case float_round_nearest_even:
            overflow_norm = false;
            inc = ((frac & roundeven_mask) != frac_lsbm1 ? frac_lsbm1 : 0);
            break;
        case float_round_ties_away:
            overflow_norm = false;
            inc = frac_lsbm1;
            break;
        case float_round_to_zero:
            overflow_norm = true;
            inc = 0;
            break;
        case float_round_up:
            inc = p.sign ? 0 : round_mask;
            overflow_norm = p.sign;
            break;
        case float_round_down:
            inc = p.sign ? round_mask : 0;
            overflow_norm = !p.sign;
            break;
        case float_round_to_odd:
            overflow_norm = true;
            inc = frac & frac_lsb ? 0 : round_mask;
            break;
        default:
            g_assert_not_reached();
        }

        exp += parm->exp_bias;
        if (likely(exp > 0)) {
            if (frac & round_mask) {
                flags |= float_flag_inexact;
                frac += inc;
                if (frac & DECOMPOSED_OVERFLOW_BIT) {
                    frac >>= 1;
                    exp++;
                }
            }
            frac >>= frac_shift;

            if (parm->arm_althp) {
                /* ARM Alt HP eschews Inf and NaN for a wider exponent.  */
                if (unlikely(exp > exp_max)) {
                    /* Overflow.  Return the maximum normal.  */
                    flags = float_flag_invalid;
                    exp = exp_max;
                    frac = -1;
                }
            } else if (unlikely(exp >= exp_max)) {
                flags |= float_flag_overflow | float_flag_inexact;
                if (overflow_norm) {
                    exp = exp_max - 1;
                    frac = -1;
                } else {
                    p.cls = float_class_inf;
                    goto do_inf;
                }
            }
        } else if (s->flush_to_zero) {
            flags |= float_flag_output_denormal;
            p.cls = float_class_zero;
            goto do_zero;
        } else {
            bool is_tiny = (s->float_detect_tininess
                            == float_tininess_before_rounding)
                        || (exp < 0)
                        || !((frac + inc) & DECOMPOSED_OVERFLOW_BIT);

            shift64RightJamming(frac, 1 - exp, &frac);
            if (frac & round_mask) {
                /* Need to recompute round-to-even.  */
                switch (s->float_rounding_mode) {
                case float_round_nearest_even:
                    inc = ((frac & roundeven_mask) != frac_lsbm1
                           ? frac_lsbm1 : 0);
                    break;
                case float_round_to_odd:
                    inc = frac & frac_lsb ? 0 : round_mask;
                    break;
                }
                flags |= float_flag_inexact;
                frac += inc;
            }

            exp = (frac & DECOMPOSED_IMPLICIT_BIT ? 1 : 0);
            frac >>= frac_shift;

            if (is_tiny && (flags & float_flag_inexact)) {
                flags |= float_flag_underflow;
            }
            if (exp == 0 && frac == 0) {
                p.cls = float_class_zero;
            }
        }
        break;

    case float_class_zero:
    do_zero:
        exp = 0;
        frac = 0;
        break;

    case float_class_inf:
    do_inf:
        assert(!parm->arm_althp);
        exp = exp_max;
        frac = 0;
        break;

    case float_class_qnan:
    case float_class_snan:
        assert(!parm->arm_althp);
        exp = exp_max;
        frac >>= parm->frac_shift;
        break;

    default:
        g_assert_not_reached();
    }

    float_raise(flags, s);
    p.exp = exp;
    p.frac = frac;
    return p;
}

/* Explicit FloatFmt version */
static FloatParts float16a_unpack_canonical(float16 f, float_status *s,
                                            const FloatFmt *params)
{
    return sf_canonicalize(float16_unpack_raw(f), params, s);
}

static FloatParts float16_unpack_canonical(float16 f, float_status *s)
{
    return float16a_unpack_canonical(f, s, &float16_params);
}

static float16 float16a_round_pack_canonical(FloatParts p, float_status *s,
                                             const FloatFmt *params)
{
    return float16_pack_raw(round_canonical(p, s, params));
}

static float16 float16_round_pack_canonical(FloatParts p, float_status *s)
{
    return float16a_round_pack_canonical(p, s, &float16_params);
}

static FloatParts float32_unpack_canonical(float32 f, float_status *s)
{
    return sf_canonicalize(float32_unpack_raw(f), &float32_params, s);
}

static float32 float32_round_pack_canonical(FloatParts p, float_status *s)
{
    return float32_pack_raw(round_canonical(p, s, &float32_params));
}

static FloatParts float64_unpack_canonical(float64 f, float_status *s)
{
    return sf_canonicalize(float64_unpack_raw(f), &float64_params, s);
}

static float64 float64_round_pack_canonical(FloatParts p, float_status *s)
{
    return float64_pack_raw(round_canonical(p, s, &float64_params));
}

static FloatParts return_nan(FloatParts a, float_status *s)
{
    switch (a.cls) {
    case float_class_snan:
        s->float_exception_flags |= float_flag_invalid;
        a = parts_silence_nan(a, s);
        /* fall through */
    case float_class_qnan:
        if (s->default_nan_mode) {
            return parts_default_nan(s);
        }
        break;

    default:
        g_assert_not_reached();
    }
    return a;
}

static FloatParts pick_nan(FloatParts a, FloatParts b, float_status *s)
{
    if (is_snan(a.cls) || is_snan(b.cls)) {
        s->float_exception_flags |= float_flag_invalid;
    }

    if (s->default_nan_mode) {
        return parts_default_nan(s);
    } else {
        if (pickNaN(a.cls, b.cls,
                    a.frac > b.frac ||
                    (a.frac == b.frac && a.sign < b.sign))) {
            a = b;
        }
        if (is_snan(a.cls)) {
            return parts_silence_nan(a, s);
        }
    }
    return a;
}

static FloatParts pick_nan_muladd(FloatParts a, FloatParts b, FloatParts c,
                                  bool inf_zero, float_status *s)
{
    int which;

    if (is_snan(a.cls) || is_snan(b.cls) || is_snan(c.cls)) {
        s->float_exception_flags |= float_flag_invalid;
    }

    which = pickNaNMulAdd(a.cls, b.cls, c.cls, inf_zero, s);

    if (s->default_nan_mode) {
        /* Note that this check is after pickNaNMulAdd so that function
         * has an opportunity to set the Invalid flag.
         */
        which = 3;
    }

    switch (which) {
    case 0:
        break;
    case 1:
        a = b;
        break;
    case 2:
        a = c;
        break;
    case 3:
        return parts_default_nan(s);
    default:
        g_assert_not_reached();
    }

    if (is_snan(a.cls)) {
        return parts_silence_nan(a, s);
    }
    return a;
}

/*
 * Returns the result of adding or subtracting the values of the
 * floating-point values `a' and `b'. The operation is performed
 * according to the IEC/IEEE Standard for Binary Floating-Point
 * Arithmetic.
 */

static FloatParts addsub_floats(FloatParts a, FloatParts b, bool subtract,
                                float_status *s)
{
    bool a_sign = a.sign;
    bool b_sign = b.sign ^ subtract;

    if (a_sign != b_sign) {
        /* Subtraction */

        if (a.cls == float_class_normal && b.cls == float_class_normal) {
            if (a.exp > b.exp || (a.exp == b.exp && a.frac >= b.frac)) {
                shift64RightJamming(b.frac, a.exp - b.exp, &b.frac);
                a.frac = a.frac - b.frac;
            } else {
                shift64RightJamming(a.frac, b.exp - a.exp, &a.frac);
                a.frac = b.frac - a.frac;
                a.exp = b.exp;
                a_sign ^= 1;
            }

            if (a.frac == 0) {
                a.cls = float_class_zero;
                a.sign = s->float_rounding_mode == float_round_down;
            } else {
                int shift = clz64(a.frac) - 1;
                a.frac = a.frac << shift;
                a.exp = a.exp - shift;
                a.sign = a_sign;
            }
            return a;
        }
        if (is_nan(a.cls) || is_nan(b.cls)) {
            return pick_nan(a, b, s);
        }
        if (a.cls == float_class_inf) {
            if (b.cls == float_class_inf) {
                float_raise(float_flag_invalid, s);
                return parts_default_nan(s);
            }
            return a;
        }
        if (a.cls == float_class_zero && b.cls == float_class_zero) {
            a.sign = s->float_rounding_mode == float_round_down;
            return a;
        }
        if (a.cls == float_class_zero || b.cls == float_class_inf) {
            b.sign = a_sign ^ 1;
            return b;
        }
        if (b.cls == float_class_zero) {
            return a;
        }
    } else {
        /* Addition */
        if (a.cls == float_class_normal && b.cls == float_class_normal) {
            if (a.exp > b.exp) {
                shift64RightJamming(b.frac, a.exp - b.exp, &b.frac);
            } else if (a.exp < b.exp) {
                shift64RightJamming(a.frac, b.exp - a.exp, &a.frac);
                a.exp = b.exp;
            }
            a.frac += b.frac;
            if (a.frac & DECOMPOSED_OVERFLOW_BIT) {
                shift64RightJamming(a.frac, 1, &a.frac);
                a.exp += 1;
            }
            return a;
        }
        if (is_nan(a.cls) || is_nan(b.cls)) {
            return pick_nan(a, b, s);
        }
        if (a.cls == float_class_inf || b.cls == float_class_zero) {
            return a;
        }
        if (b.cls == float_class_inf || a.cls == float_class_zero) {
            b.sign = b_sign;
            return b;
        }
    }
    g_assert_not_reached();
}

/*
 * Returns the result of adding or subtracting the floating-point
 * values `a' and `b'. The operation is performed according to the
 * IEC/IEEE Standard for Binary Floating-Point Arithmetic.
 */

float16 QEMU_FLATTEN float16_add(float16 a, float16 b, float_status *status)
{
    FloatParts pa = float16_unpack_canonical(a, status);
    FloatParts pb = float16_unpack_canonical(b, status);
    FloatParts pr = addsub_floats(pa, pb, false, status);

    return float16_round_pack_canonical(pr, status);
}

float16 QEMU_FLATTEN float16_sub(float16 a, float16 b, float_status *status)
{
    FloatParts pa = float16_unpack_canonical(a, status);
    FloatParts pb = float16_unpack_canonical(b, status);
    FloatParts pr = addsub_floats(pa, pb, true, status);

    return float16_round_pack_canonical(pr, status);
}

static float32 QEMU_SOFTFLOAT_ATTR
soft_f32_addsub(float32 a, float32 b, bool subtract, float_status *status)
{
    FloatParts pa = float32_unpack_canonical(a, status);
    FloatParts pb = float32_unpack_canonical(b, status);
    FloatParts pr = addsub_floats(pa, pb, subtract, status);

    return float32_round_pack_canonical(pr, status);
}

static inline float32 soft_f32_add(float32 a, float32 b, float_status *status)
{
    return soft_f32_addsub(a, b, false, status);
}

static inline float32 soft_f32_sub(float32 a, float32 b, float_status *status)
{
    return soft_f32_addsub(a, b, true, status);
}

static float64 QEMU_SOFTFLOAT_ATTR
soft_f64_addsub(float64 a, float64 b, bool subtract, float_status *status)
{
    FloatParts pa = float64_unpack_canonical(a, status);
    FloatParts pb = float64_unpack_canonical(b, status);
    FloatParts pr = addsub_floats(pa, pb, subtract, status);

    return float64_round_pack_canonical(pr, status);
}

static inline float64 soft_f64_add(float64 a, float64 b, float_status *status)
{
    return soft_f64_addsub(a, b, false, status);
}

static inline float64 soft_f64_sub(float64 a, float64 b, float_status *status)
{
    return soft_f64_addsub(a, b, true, status);
}

static float hard_f32_add(float a, float b)
{
    return a + b;
}

static float hard_f32_sub(float a, float b)
{
    return a - b;
}

static double hard_f64_add(double a, double b)
{
    return a + b;
}

static double hard_f64_sub(double a, double b)
{
    return a - b;
}

static bool f32_addsub_post(union_float32 a, union_float32 b)
{
    if (QEMU_HARDFLOAT_2F32_USE_FP) {
        return !(fpclassify(a.h) == FP_ZERO && fpclassify(b.h) == FP_ZERO);
    }
    return !(float32_is_zero(a.s) && float32_is_zero(b.s));
}

static bool f64_addsub_post(union_float64 a, union_float64 b)
{
    if (QEMU_HARDFLOAT_2F64_USE_FP) {
        return !(fpclassify(a.h) == FP_ZERO && fpclassify(b.h) == FP_ZERO);
    } else {
        return !(float64_is_zero(a.s) && float64_is_zero(b.s));
    }
}

static float32 float32_addsub(float32 a, float32 b, float_status *s,
                              hard_f32_op2_fn hard, soft_f32_op2_fn soft)
{
    return float32_gen2(a, b, s, hard, soft,
                        f32_is_zon2, f32_addsub_post, NULL, NULL);
}

static float64 float64_addsub(float64 a, float64 b, float_status *s,
                              hard_f64_op2_fn hard, soft_f64_op2_fn soft)
{
    return float64_gen2(a, b, s, hard, soft,
                        f64_is_zon2, f64_addsub_post, NULL, NULL);
}

float32 QEMU_FLATTEN
float32_add(float32 a, float32 b, float_status *s)
{
    return float32_addsub(a, b, s, hard_f32_add, soft_f32_add);
}

float32 QEMU_FLATTEN
float32_sub(float32 a, float32 b, float_status *s)
{
    return float32_addsub(a, b, s, hard_f32_sub, soft_f32_sub);
}

float64 QEMU_FLATTEN
float64_add(float64 a, float64 b, float_status *s)
{
    return float64_addsub(a, b, s, hard_f64_add, soft_f64_add);
}

float64 QEMU_FLATTEN
float64_sub(float64 a, float64 b, float_status *s)
{
    return float64_addsub(a, b, s, hard_f64_sub, soft_f64_sub);
}

/*
 * Returns the result of multiplying the floating-point values `a' and
 * `b'. The operation is performed according to the IEC/IEEE Standard
 * for Binary Floating-Point Arithmetic.
 */

static FloatParts mul_floats(FloatParts a, FloatParts b, float_status *s)
{
    bool sign = a.sign ^ b.sign;

    if (a.cls == float_class_normal && b.cls == float_class_normal) {
        uint64_t hi, lo;
        int exp = a.exp + b.exp;

        mul64To128(a.frac, b.frac, &hi, &lo);
        shift128RightJamming(hi, lo, DECOMPOSED_BINARY_POINT, &hi, &lo);
        if (lo & DECOMPOSED_OVERFLOW_BIT) {
            shift64RightJamming(lo, 1, &lo);
            exp += 1;
        }

        /* Re-use a */
        a.exp = exp;
        a.sign = sign;
        a.frac = lo;
        return a;
    }
    /* handle all the NaN cases */
    if (is_nan(a.cls) || is_nan(b.cls)) {
        return pick_nan(a, b, s);
    }
    /* Inf * Zero == NaN */
    if ((a.cls == float_class_inf && b.cls == float_class_zero) ||
        (a.cls == float_class_zero && b.cls == float_class_inf)) {
        s->float_exception_flags |= float_flag_invalid;
        return parts_default_nan(s);
    }
    /* Multiply by 0 or Inf */
    if (a.cls == float_class_inf || a.cls == float_class_zero) {
        a.sign = sign;
        return a;
    }
    if (b.cls == float_class_inf || b.cls == float_class_zero) {
        b.sign = sign;
        return b;
    }
    g_assert_not_reached();
}

float16 QEMU_FLATTEN float16_mul(float16 a, float16 b, float_status *status)
{
    FloatParts pa = float16_unpack_canonical(a, status);
    FloatParts pb = float16_unpack_canonical(b, status);
    FloatParts pr = mul_floats(pa, pb, status);

    return float16_round_pack_canonical(pr, status);
}

static float32 QEMU_SOFTFLOAT_ATTR
soft_f32_mul(float32 a, float32 b, float_status *status)
{
    FloatParts pa = float32_unpack_canonical(a, status);
    FloatParts pb = float32_unpack_canonical(b, status);
    FloatParts pr = mul_floats(pa, pb, status);

    return float32_round_pack_canonical(pr, status);
}

static float64 QEMU_SOFTFLOAT_ATTR
soft_f64_mul(float64 a, float64 b, float_status *status)
{
    FloatParts pa = float64_unpack_canonical(a, status);
    FloatParts pb = float64_unpack_canonical(b, status);
    FloatParts pr = mul_floats(pa, pb, status);

    return float64_round_pack_canonical(pr, status);
}

static float hard_f32_mul(float a, float b)
{
    return a * b;
}

static double hard_f64_mul(double a, double b)
{
    return a * b;
}

static bool f32_mul_fast_test(union_float32 a, union_float32 b)
{
    return float32_is_zero(a.s) || float32_is_zero(b.s);
}

static bool f64_mul_fast_test(union_float64 a, union_float64 b)
{
    return float64_is_zero(a.s) || float64_is_zero(b.s);
}

static float32 f32_mul_fast_op(float32 a, float32 b, float_status *s)
{
    bool signbit = float32_is_neg(a) ^ float32_is_neg(b);

    return float32_set_sign(float32_zero, signbit);
}

static float64 f64_mul_fast_op(float64 a, float64 b, float_status *s)
{
    bool signbit = float64_is_neg(a) ^ float64_is_neg(b);

    return float64_set_sign(float64_zero, signbit);
}

float32 QEMU_FLATTEN
float32_mul(float32 a, float32 b, float_status *s)
{
    return float32_gen2(a, b, s, hard_f32_mul, soft_f32_mul,
                        f32_is_zon2, NULL, f32_mul_fast_test, f32_mul_fast_op);
}

float64 QEMU_FLATTEN
float64_mul(float64 a, float64 b, float_status *s)
{
    return float64_gen2(a, b, s, hard_f64_mul, soft_f64_mul,
                        f64_is_zon2, NULL, f64_mul_fast_test, f64_mul_fast_op);
}

/*
 * Returns the result of multiplying the floating-point values `a' and
 * `b' then adding 'c', with no intermediate rounding step after the
 * multiplication. The operation is performed according to the
 * IEC/IEEE Standard for Binary Floating-Point Arithmetic 754-2008.
 * The flags argument allows the caller to select negation of the
 * addend, the intermediate product, or the final result. (The
 * difference between this and having the caller do a separate
 * negation is that negating externally will flip the sign bit on
 * NaNs.)
 */

static FloatParts muladd_floats(FloatParts a, FloatParts b, FloatParts c,
                                int flags, float_status *s)
{
    bool inf_zero = ((1 << a.cls) | (1 << b.cls)) ==
                    ((1 << float_class_inf) | (1 << float_class_zero));
    bool p_sign;
    bool sign_flip = flags & float_muladd_negate_result;
    FloatClass p_class;
    uint64_t hi, lo;
    int p_exp;

    /* It is implementation-defined whether the cases of (0,inf,qnan)
     * and (inf,0,qnan) raise InvalidOperation or not (and what QNaN
     * they return if they do), so we have to hand this information
     * off to the target-specific pick-a-NaN routine.
     */
    if (is_nan(a.cls) || is_nan(b.cls) || is_nan(c.cls)) {
        return pick_nan_muladd(a, b, c, inf_zero, s);
    }

    if (inf_zero) {
        s->float_exception_flags |= float_flag_invalid;
        return parts_default_nan(s);
    }

    if (flags & float_muladd_negate_c) {
        c.sign ^= 1;
    }

    p_sign = a.sign ^ b.sign;

    if (flags & float_muladd_negate_product) {
        p_sign ^= 1;
    }

    if (a.cls == float_class_inf || b.cls == float_class_inf) {
        p_class = float_class_inf;
    } else if (a.cls == float_class_zero || b.cls == float_class_zero) {
        p_class = float_class_zero;
    } else {
        p_class = float_class_normal;
    }

    if (c.cls == float_class_inf) {
        if (p_class == float_class_inf && p_sign != c.sign) {
            s->float_exception_flags |= float_flag_invalid;
            return parts_default_nan(s);
        } else {
            a.cls = float_class_inf;
            a.sign = c.sign ^ sign_flip;
            return a;
        }
    }

    if (p_class == float_class_inf) {
        a.cls = float_class_inf;
        a.sign = p_sign ^ sign_flip;
        return a;
    }

    if (p_class == float_class_zero) {
        if (c.cls == float_class_zero) {
            if (p_sign != c.sign) {
                p_sign = s->float_rounding_mode == float_round_down;
            }
            c.sign = p_sign;
        } else if (flags & float_muladd_halve_result) {
            c.exp -= 1;
        }
        c.sign ^= sign_flip;
        return c;
    }

    /* a & b should be normals now... */
    assert(a.cls == float_class_normal &&
           b.cls == float_class_normal);

    p_exp = a.exp + b.exp;

    /* Multiply of 2 62-bit numbers produces a (2*62) == 124-bit
     * result.
     */
    mul64To128(a.frac, b.frac, &hi, &lo);
    /* binary point now at bit 124 */

    /* check for overflow */
    if (hi & (1ULL << (DECOMPOSED_BINARY_POINT * 2 + 1 - 64))) {
        shift128RightJamming(hi, lo, 1, &hi, &lo);
        p_exp += 1;
    }

    /* + add/sub */
    if (c.cls == float_class_zero) {
        /* move binary point back to 62 */
        shift128RightJamming(hi, lo, DECOMPOSED_BINARY_POINT, &hi, &lo);
    } else {
        int exp_diff = p_exp - c.exp;
        if (p_sign == c.sign) {
            /* Addition */
            if (exp_diff <= 0) {
                shift128RightJamming(hi, lo,
                                     DECOMPOSED_BINARY_POINT - exp_diff,
                                     &hi, &lo);
                lo += c.frac;
                p_exp = c.exp;
            } else {
                uint64_t c_hi, c_lo;
                /* shift c to the same binary point as the product (124) */
                c_hi = c.frac >> 2;
                c_lo = 0;
                shift128RightJamming(c_hi, c_lo,
                                     exp_diff,
                                     &c_hi, &c_lo);
                add128(hi, lo, c_hi, c_lo, &hi, &lo);
                /* move binary point back to 62 */
                shift128RightJamming(hi, lo, DECOMPOSED_BINARY_POINT, &hi, &lo);
            }

            if (lo & DECOMPOSED_OVERFLOW_BIT) {
                shift64RightJamming(lo, 1, &lo);
                p_exp += 1;
            }

        } else {
            /* Subtraction */
            uint64_t c_hi, c_lo;
            /* make C binary point match product at bit 124 */
            c_hi = c.frac >> 2;
            c_lo = 0;

            if (exp_diff <= 0) {
                shift128RightJamming(hi, lo, -exp_diff, &hi, &lo);
                if (exp_diff == 0
                    &&
                    (hi > c_hi || (hi == c_hi && lo >= c_lo))) {
                    sub128(hi, lo, c_hi, c_lo, &hi, &lo);
                } else {
                    sub128(c_hi, c_lo, hi, lo, &hi, &lo);
                    p_sign ^= 1;
                    p_exp = c.exp;
                }
            } else {
                shift128RightJamming(c_hi, c_lo,
                                     exp_diff,
                                     &c_hi, &c_lo);
                sub128(hi, lo, c_hi, c_lo, &hi, &lo);
            }

            if (hi == 0 && lo == 0) {
                a.cls = float_class_zero;
                a.sign = s->float_rounding_mode == float_round_down;
                a.sign ^= sign_flip;
                return a;
            } else {
                int shift;
                if (hi != 0) {
                    shift = clz64(hi);
                } else {
                    shift = clz64(lo) + 64;
                }
                /* Normalizing to a binary point of 124 is the
                   correct adjust for the exponent.  However since we're
                   shifting, we might as well put the binary point back
                   at 62 where we really want it.  Therefore shift as
                   if we're leaving 1 bit at the top of the word, but
                   adjust the exponent as if we're leaving 3 bits.  */
                shift -= 1;
                if (shift >= 64) {
                    lo = lo << (shift - 64);
                } else {
                    hi = (hi << shift) | (lo >> (64 - shift));
                    lo = hi | ((lo << shift) != 0);
                }
                p_exp -= shift - 2;
            }
        }
    }

    if (flags & float_muladd_halve_result) {
        p_exp -= 1;
    }

    /* finally prepare our result */
    a.cls = float_class_normal;
    a.sign = p_sign ^ sign_flip;
    a.exp = p_exp;
    a.frac = lo;

    return a;
}

float16 QEMU_FLATTEN float16_muladd(float16 a, float16 b, float16 c,
                                                int flags, float_status *status)
{
    FloatParts pa = float16_unpack_canonical(a, status);
    FloatParts pb = float16_unpack_canonical(b, status);
    FloatParts pc = float16_unpack_canonical(c, status);
    FloatParts pr = muladd_floats(pa, pb, pc, flags, status);

    return float16_round_pack_canonical(pr, status);
}

static float32 QEMU_SOFTFLOAT_ATTR
soft_f32_muladd(float32 a, float32 b, float32 c, int flags,
                float_status *status)
{
    FloatParts pa = float32_unpack_canonical(a, status);
    FloatParts pb = float32_unpack_canonical(b, status);
    FloatParts pc = float32_unpack_canonical(c, status);
    FloatParts pr = muladd_floats(pa, pb, pc, flags, status);

    return float32_round_pack_canonical(pr, status);
}

static float64 QEMU_SOFTFLOAT_ATTR
soft_f64_muladd(float64 a, float64 b, float64 c, int flags,
                float_status *status)
{
    FloatParts pa = float64_unpack_canonical(a, status);
    FloatParts pb = float64_unpack_canonical(b, status);
    FloatParts pc = float64_unpack_canonical(c, status);
    FloatParts pr = muladd_floats(pa, pb, pc, flags, status);

    return float64_round_pack_canonical(pr, status);
}

static bool force_soft_fma;

float32 QEMU_FLATTEN
float32_muladd(float32 xa, float32 xb, float32 xc, int flags, float_status *s)
{
    union_float32 ua, ub, uc, ur;

    ua.s = xa;
    ub.s = xb;
    uc.s = xc;

    if (unlikely(!can_use_fpu(s))) {
        goto soft;
    }
    if (unlikely(flags & float_muladd_halve_result)) {
        goto soft;
    }

    float32_input_flush3(&ua.s, &ub.s, &uc.s, s);
    if (unlikely(!f32_is_zon3(ua, ub, uc))) {
        goto soft;
    }

    if (unlikely(force_soft_fma)) {
        goto soft;
    }

    /*
     * When (a || b) == 0, there's no need to check for under/over flow,
     * since we know the addend is (normal || 0) and the product is 0.
     */
    if (float32_is_zero(ua.s) || float32_is_zero(ub.s)) {
        union_float32 up;
        bool prod_sign;

        prod_sign = float32_is_neg(ua.s) ^ float32_is_neg(ub.s);
        prod_sign ^= !!(flags & float_muladd_negate_product);
        up.s = float32_set_sign(float32_zero, prod_sign);

        if (flags & float_muladd_negate_c) {
            uc.h = -uc.h;
        }
        ur.h = up.h + uc.h;
    } else {
        union_float32 ua_orig = ua;
        union_float32 uc_orig = uc;

        if (flags & float_muladd_negate_product) {
            ua.h = -ua.h;
        }
        if (flags & float_muladd_negate_c) {
            uc.h = -uc.h;
        }

        ur.h = fmaf(ua.h, ub.h, uc.h);

        if (unlikely(f32_is_inf(ur))) {
            s->float_exception_flags |= float_flag_overflow;
        } else if (unlikely(fabsf(ur.h) <= FLT_MIN)) {
            ua = ua_orig;
            uc = uc_orig;
            goto soft;
        }
    }
    if (flags & float_muladd_negate_result) {
        return float32_chs(ur.s);
    }
    return ur.s;

 soft:
    return soft_f32_muladd(ua.s, ub.s, uc.s, flags, s);
}

float64 QEMU_FLATTEN
float64_muladd(float64 xa, float64 xb, float64 xc, int flags, float_status *s)
{
    union_float64 ua, ub, uc, ur;

    ua.s = xa;
    ub.s = xb;
    uc.s = xc;

    if (unlikely(!can_use_fpu(s))) {
        goto soft;
    }
    if (unlikely(flags & float_muladd_halve_result)) {
        goto soft;
    }

    float64_input_flush3(&ua.s, &ub.s, &uc.s, s);
    if (unlikely(!f64_is_zon3(ua, ub, uc))) {
        goto soft;
    }

    if (unlikely(force_soft_fma)) {
        goto soft;
    }

    /*
     * When (a || b) == 0, there's no need to check for under/over flow,
     * since we know the addend is (normal || 0) and the product is 0.
     */
    if (float64_is_zero(ua.s) || float64_is_zero(ub.s)) {
        union_float64 up;
        bool prod_sign;

        prod_sign = float64_is_neg(ua.s) ^ float64_is_neg(ub.s);
        prod_sign ^= !!(flags & float_muladd_negate_product);
        up.s = float64_set_sign(float64_zero, prod_sign);

        if (flags & float_muladd_negate_c) {
            uc.h = -uc.h;
        }
        ur.h = up.h + uc.h;
    } else {
        union_float64 ua_orig = ua;
        union_float64 uc_orig = uc;

        if (flags & float_muladd_negate_product) {
            ua.h = -ua.h;
        }
        if (flags & float_muladd_negate_c) {
            uc.h = -uc.h;
        }

        ur.h = fma(ua.h, ub.h, uc.h);

        if (unlikely(f64_is_inf(ur))) {
            s->float_exception_flags |= float_flag_overflow;
        } else if (unlikely(fabs(ur.h) <= FLT_MIN)) {
            ua = ua_orig;
            uc = uc_orig;
            goto soft;
        }
    }
    if (flags & float_muladd_negate_result) {
        return float64_chs(ur.s);
    }
    return ur.s;

 soft:
    return soft_f64_muladd(ua.s, ub.s, uc.s, flags, s);
}

/*
 * Returns the result of dividing the floating-point value `a' by the
 * corresponding value `b'. The operation is performed according to
 * the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
 */

static FloatParts div_floats(FloatParts a, FloatParts b, float_status *s)
{
    bool sign = a.sign ^ b.sign;

    if (a.cls == float_class_normal && b.cls == float_class_normal) {
        uint64_t n0, n1, q, r;
        int exp = a.exp - b.exp;

        /*
         * We want a 2*N / N-bit division to produce exactly an N-bit
         * result, so that we do not lose any precision and so that we
         * do not have to renormalize afterward.  If A.frac < B.frac,
         * then division would produce an (N-1)-bit result; shift A left
         * by one to produce the an N-bit result, and decrement the
         * exponent to match.
         *
         * The udiv_qrnnd algorithm that we're using requires normalization,
         * i.e. the msb of the denominator must be set.  Since we know that
         * DECOMPOSED_BINARY_POINT is msb-1, the inputs must be shifted left
         * by one (more), and the remainder must be shifted right by one.
         */
        if (a.frac < b.frac) {
            exp -= 1;
            shift128Left(0, a.frac, DECOMPOSED_BINARY_POINT + 2, &n1, &n0);
        } else {
            shift128Left(0, a.frac, DECOMPOSED_BINARY_POINT + 1, &n1, &n0);
        }
        q = udiv_qrnnd(&r, n1, n0, b.frac << 1);

        /*
         * Set lsb if there is a remainder, to set inexact.
         * As mentioned above, to find the actual value of the remainder we
         * would need to shift right, but (1) we are only concerned about
         * non-zero-ness, and (2) the remainder will always be even because
         * both inputs to the division primitive are even.
         */
        a.frac = q | (r != 0);
        a.sign = sign;
        a.exp = exp;
        return a;
    }
    /* handle all the NaN cases */
    if (is_nan(a.cls) || is_nan(b.cls)) {
        return pick_nan(a, b, s);
    }
    /* 0/0 or Inf/Inf */
    if (a.cls == b.cls
        &&
        (a.cls == float_class_inf || a.cls == float_class_zero)) {
        s->float_exception_flags |= float_flag_invalid;
        return parts_default_nan(s);
    }
    /* Inf / x or 0 / x */
    if (a.cls == float_class_inf || a.cls == float_class_zero) {
        a.sign = sign;
        return a;
    }
    /* Div 0 => Inf */
    if (b.cls == float_class_zero) {
        s->float_exception_flags |= float_flag_divbyzero;
        a.cls = float_class_inf;
        a.sign = sign;
        return a;
    }
    /* Div by Inf */
    if (b.cls == float_class_inf) {
        a.cls = float_class_zero;
        a.sign = sign;
        return a;
    }
    g_assert_not_reached();
}

float16 float16_div(float16 a, float16 b, float_status *status)
{
    FloatParts pa = float16_unpack_canonical(a, status);
    FloatParts pb = float16_unpack_canonical(b, status);
    FloatParts pr = div_floats(pa, pb, status);

    return float16_round_pack_canonical(pr, status);
}

static float32 QEMU_SOFTFLOAT_ATTR
soft_f32_div(float32 a, float32 b, float_status *status)
{
    FloatParts pa = float32_unpack_canonical(a, status);
    FloatParts pb = float32_unpack_canonical(b, status);
    FloatParts pr = div_floats(pa, pb, status);

    return float32_round_pack_canonical(pr, status);
}

static float64 QEMU_SOFTFLOAT_ATTR
soft_f64_div(float64 a, float64 b, float_status *status)
{
    FloatParts pa = float64_unpack_canonical(a, status);
    FloatParts pb = float64_unpack_canonical(b, status);
    FloatParts pr = div_floats(pa, pb, status);

    return float64_round_pack_canonical(pr, status);
}

static float hard_f32_div(float a, float b)
{
    return a / b;
}

static double hard_f64_div(double a, double b)
{
    return a / b;
}

static bool f32_div_pre(union_float32 a, union_float32 b)
{
    if (QEMU_HARDFLOAT_2F32_USE_FP) {
        return (fpclassify(a.h) == FP_NORMAL || fpclassify(a.h) == FP_ZERO) &&
               fpclassify(b.h) == FP_NORMAL;
    }
    return float32_is_zero_or_normal(a.s) && float32_is_normal(b.s);
}

static bool f64_div_pre(union_float64 a, union_float64 b)
{
    if (QEMU_HARDFLOAT_2F64_USE_FP) {
        return (fpclassify(a.h) == FP_NORMAL || fpclassify(a.h) == FP_ZERO) &&
               fpclassify(b.h) == FP_NORMAL;
    }
    return float64_is_zero_or_normal(a.s) && float64_is_normal(b.s);
}

static bool f32_div_post(union_float32 a, union_float32 b)
{
    if (QEMU_HARDFLOAT_2F32_USE_FP) {
        return fpclassify(a.h) != FP_ZERO;
    }
    return !float32_is_zero(a.s);
}

static bool f64_div_post(union_float64 a, union_float64 b)
{
    if (QEMU_HARDFLOAT_2F64_USE_FP) {
        return fpclassify(a.h) != FP_ZERO;
    }
    return !float64_is_zero(a.s);
}

float32 QEMU_FLATTEN
float32_div(float32 a, float32 b, float_status *s)
{
    return float32_gen2(a, b, s, hard_f32_div, soft_f32_div,
                        f32_div_pre, f32_div_post, NULL, NULL);
}

float64 QEMU_FLATTEN
float64_div(float64 a, float64 b, float_status *s)
{
    return float64_gen2(a, b, s, hard_f64_div, soft_f64_div,
                        f64_div_pre, f64_div_post, NULL, NULL);
}

/*
 * Float to Float conversions
 *
 * Returns the result of converting one float format to another. The
 * conversion is performed according to the IEC/IEEE Standard for
 * Binary Floating-Point Arithmetic.
 *
 * The float_to_float helper only needs to take care of raising
 * invalid exceptions and handling the conversion on NaNs.
 */

static FloatParts float_to_float(FloatParts a, const FloatFmt *dstf,
                                 float_status *s)
{
    if (dstf->arm_althp) {
        switch (a.cls) {
        case float_class_qnan:
        case float_class_snan:
            /* There is no NaN in the destination format.  Raise Invalid
             * and return a zero with the sign of the input NaN.
             */
            s->float_exception_flags |= float_flag_invalid;
            a.cls = float_class_zero;
            a.frac = 0;
            a.exp = 0;
            break;

        case float_class_inf:
            /* There is no Inf in the destination format.  Raise Invalid
             * and return the maximum normal with the correct sign.
             */
            s->float_exception_flags |= float_flag_invalid;
            a.cls = float_class_normal;
            a.exp = dstf->exp_max;
            a.frac = ((1ull << dstf->frac_size) - 1) << dstf->frac_shift;
            break;

        default:
            break;
        }
    } else if (is_nan(a.cls)) {
        if (is_snan(a.cls)) {
            s->float_exception_flags |= float_flag_invalid;
            a = parts_silence_nan(a, s);
        }
        if (s->default_nan_mode) {
            return parts_default_nan(s);
        }
    }
    return a;
}

float32 float16_to_float32(float16 a, bool ieee, float_status *s)
{
    const FloatFmt *fmt16 = ieee ? &float16_params : &float16_params_ahp;
    FloatParts p = float16a_unpack_canonical(a, s, fmt16);
    FloatParts pr = float_to_float(p, &float32_params, s);
    return float32_round_pack_canonical(pr, s);
}

float64 float16_to_float64(float16 a, bool ieee, float_status *s)
{
    const FloatFmt *fmt16 = ieee ? &float16_params : &float16_params_ahp;
    FloatParts p = float16a_unpack_canonical(a, s, fmt16);
    FloatParts pr = float_to_float(p, &float64_params, s);
    return float64_round_pack_canonical(pr, s);
}

float16 float32_to_float16(float32 a, bool ieee, float_status *s)
{
    const FloatFmt *fmt16 = ieee ? &float16_params : &float16_params_ahp;
    FloatParts p = float32_unpack_canonical(a, s);
    FloatParts pr = float_to_float(p, fmt16, s);
    return float16a_round_pack_canonical(pr, s, fmt16);
}

static float64 QEMU_SOFTFLOAT_ATTR
soft_float32_to_float64(float32 a, float_status *s)
{
    FloatParts p = float32_unpack_canonical(a, s);
    FloatParts pr = float_to_float(p, &float64_params, s);
    return float64_round_pack_canonical(pr, s);
}

float64 float32_to_float64(float32 a, float_status *s)
{
    if (likely(float32_is_normal(a))) {
        /* Widening conversion can never produce inexact results.  */
        union_float32 uf;
        union_float64 ud;
        uf.s = a;
        ud.h = uf.h;
        return ud.s;
    } else if (float32_is_zero(a)) {
        return float64_set_sign(float64_zero, float32_is_neg(a));
    } else {
        return soft_float32_to_float64(a, s);
    }
}

float16 float64_to_float16(float64 a, bool ieee, float_status *s)
{
    const FloatFmt *fmt16 = ieee ? &float16_params : &float16_params_ahp;
    FloatParts p = float64_unpack_canonical(a, s);
    FloatParts pr = float_to_float(p, fmt16, s);
    return float16a_round_pack_canonical(pr, s, fmt16);
}

float32 float64_to_float32(float64 a, float_status *s)
{
    FloatParts p = float64_unpack_canonical(a, s);
    FloatParts pr = float_to_float(p, &float32_params, s);
    return float32_round_pack_canonical(pr, s);
}

/*
 * Rounds the floating-point value `a' to an integer, and returns the
 * result as a floating-point value. The operation is performed
 * according to the IEC/IEEE Standard for Binary Floating-Point
 * Arithmetic.
 */

static FloatParts round_to_int(FloatParts a, int rmode,
                               int scale, float_status *s)
{
    switch (a.cls) {
    case float_class_qnan:
    case float_class_snan:
        return return_nan(a, s);

    case float_class_zero:
    case float_class_inf:
        /* already "integral" */
        break;

    case float_class_normal:
        scale = MIN(MAX(scale, -0x10000), 0x10000);
        a.exp += scale;

        if (a.exp >= DECOMPOSED_BINARY_POINT) {
            /* already integral */
            break;
        }
        if (a.exp < 0) {
            bool one;
            /* all fractional */
            s->float_exception_flags |= float_flag_inexact;
            switch (rmode) {
            case float_round_nearest_even:
                one = a.exp == -1 && a.frac > DECOMPOSED_IMPLICIT_BIT;
                break;
            case float_round_ties_away:
                one = a.exp == -1 && a.frac >= DECOMPOSED_IMPLICIT_BIT;
                break;
            case float_round_to_zero:
                one = false;
                break;
            case float_round_up:
                one = !a.sign;
                break;
            case float_round_down:
                one = a.sign;
                break;
            case float_round_to_odd:
                one = true;
                break;
            default:
                g_assert_not_reached();
            }

            if (one) {
                a.frac = DECOMPOSED_IMPLICIT_BIT;
                a.exp = 0;
            } else {
                a.cls = float_class_zero;
            }
        } else {
            uint64_t frac_lsb = DECOMPOSED_IMPLICIT_BIT >> a.exp;
            uint64_t frac_lsbm1 = frac_lsb >> 1;
            uint64_t rnd_even_mask = (frac_lsb - 1) | frac_lsb;
            uint64_t rnd_mask = rnd_even_mask >> 1;
            uint64_t inc;

            switch (rmode) {
            case float_round_nearest_even:
                inc = ((a.frac & rnd_even_mask) != frac_lsbm1 ? frac_lsbm1 : 0);
                break;
            case float_round_ties_away:
                inc = frac_lsbm1;
                break;
            case float_round_to_zero:
                inc = 0;
                break;
            case float_round_up:
                inc = a.sign ? 0 : rnd_mask;
                break;
            case float_round_down:
                inc = a.sign ? rnd_mask : 0;
                break;
            case float_round_to_odd:
                inc = a.frac & frac_lsb ? 0 : rnd_mask;
                break;
            default:
                g_assert_not_reached();
            }

            if (a.frac & rnd_mask) {
                s->float_exception_flags |= float_flag_inexact;
                a.frac += inc;
                a.frac &= ~rnd_mask;
                if (a.frac & DECOMPOSED_OVERFLOW_BIT) {
                    a.frac >>= 1;
                    a.exp++;
                }
            }
        }
        break;
    default:
        g_assert_not_reached();
    }
    return a;
}

float16 float16_round_to_int(float16 a, float_status *s)
{
    FloatParts pa = float16_unpack_canonical(a, s);
    FloatParts pr = round_to_int(pa, s->float_rounding_mode, 0, s);
    return float16_round_pack_canonical(pr, s);
}

float32 float32_round_to_int(float32 a, float_status *s)
{
    FloatParts pa = float32_unpack_canonical(a, s);
    FloatParts pr = round_to_int(pa, s->float_rounding_mode, 0, s);
    return float32_round_pack_canonical(pr, s);
}

float64 float64_round_to_int(float64 a, float_status *s)
{
    FloatParts pa = float64_unpack_canonical(a, s);
    FloatParts pr = round_to_int(pa, s->float_rounding_mode, 0, s);
    return float64_round_pack_canonical(pr, s);
}

/*
 * Returns the result of converting the floating-point value `a' to
 * the two's complement integer format. The conversion is performed
 * according to the IEC/IEEE Standard for Binary Floating-Point
 * Arithmetic---which means in particular that the conversion is
 * rounded according to the current rounding mode. If `a' is a NaN,
 * the largest positive integer is returned. Otherwise, if the
 * conversion overflows, the largest integer with the same sign as `a'
 * is returned.
*/

static int64_t round_to_int_and_pack(FloatParts in, int rmode, int scale,
                                     int64_t min, int64_t max,
                                     float_status *s)
{
    uint64_t r;
    int orig_flags = get_float_exception_flags(s);
    FloatParts p = round_to_int(in, rmode, scale, s);

    switch (p.cls) {
    case float_class_snan:
    case float_class_qnan:
        s->float_exception_flags = orig_flags | float_flag_invalid;
        return max;
    case float_class_inf:
        s->float_exception_flags = orig_flags | float_flag_invalid;
        return p.sign ? min : max;
    case float_class_zero:
        return 0;
    case float_class_normal:
        if (p.exp < DECOMPOSED_BINARY_POINT) {
            r = p.frac >> (DECOMPOSED_BINARY_POINT - p.exp);
        } else if (p.exp - DECOMPOSED_BINARY_POINT < 2) {
            r = p.frac << (p.exp - DECOMPOSED_BINARY_POINT);
        } else {
            r = UINT64_MAX;
        }
        if (p.sign) {
            if (r <= -(uint64_t) min) {
                return -r;
            } else {
                s->float_exception_flags = orig_flags | float_flag_invalid;
                return min;
            }
        } else {
            if (r <= max) {
                return r;
            } else {
                s->float_exception_flags = orig_flags | float_flag_invalid;
                return max;
            }
        }
    default:
        g_assert_not_reached();
    }
}

int16_t float16_to_int16_scalbn(float16 a, int rmode, int scale,
                                float_status *s)
{
    return round_to_int_and_pack(float16_unpack_canonical(a, s),
                                 rmode, scale, INT16_MIN, INT16_MAX, s);
}

int32_t float16_to_int32_scalbn(float16 a, int rmode, int scale,
                                float_status *s)
{
    return round_to_int_and_pack(float16_unpack_canonical(a, s),
                                 rmode, scale, INT32_MIN, INT32_MAX, s);
}

int64_t float16_to_int64_scalbn(float16 a, int rmode, int scale,
                                float_status *s)
{
    return round_to_int_and_pack(float16_unpack_canonical(a, s),
                                 rmode, scale, INT64_MIN, INT64_MAX, s);
}

int16_t float32_to_int16_scalbn(float32 a, int rmode, int scale,
                                float_status *s)
{
    return round_to_int_and_pack(float32_unpack_canonical(a, s),
                                 rmode, scale, INT16_MIN, INT16_MAX, s);
}

int32_t float32_to_int32_scalbn(float32 a, int rmode, int scale,
                                float_status *s)
{
    return round_to_int_and_pack(float32_unpack_canonical(a, s),
                                 rmode, scale, INT32_MIN, INT32_MAX, s);
}

int64_t float32_to_int64_scalbn(float32 a, int rmode, int scale,
                                float_status *s)
{
    return round_to_int_and_pack(float32_unpack_canonical(a, s),
                                 rmode, scale, INT64_MIN, INT64_MAX, s);
}

int16_t float64_to_int16_scalbn(float64 a, int rmode, int scale,
                                float_status *s)
{
    return round_to_int_and_pack(float64_unpack_canonical(a, s),
                                 rmode, scale, INT16_MIN, INT16_MAX, s);
}

int32_t float64_to_int32_scalbn(float64 a, int rmode, int scale,
                                float_status *s)
{
    return round_to_int_and_pack(float64_unpack_canonical(a, s),
                                 rmode, scale, INT32_MIN, INT32_MAX, s);
}

int64_t float64_to_int64_scalbn(float64 a, int rmode, int scale,
                                float_status *s)
{
    return round_to_int_and_pack(float64_unpack_canonical(a, s),
                                 rmode, scale, INT64_MIN, INT64_MAX, s);
}

int16_t float16_to_int16(float16 a, float_status *s)
{
    return float16_to_int16_scalbn(a, s->float_rounding_mode, 0, s);
}

int32_t float16_to_int32(float16 a, float_status *s)
{
    return float16_to_int32_scalbn(a, s->float_rounding_mode, 0, s);
}

int64_t float16_to_int64(float16 a, float_status *s)
{
    return float16_to_int64_scalbn(a, s->float_rounding_mode, 0, s);
}

int16_t float32_to_int16(float32 a, float_status *s)
{
    return float32_to_int16_scalbn(a, s->float_rounding_mode, 0, s);
}

int32_t float32_to_int32(float32 a, float_status *s)
{
    return float32_to_int32_scalbn(a, s->float_rounding_mode, 0, s);
}

int64_t float32_to_int64(float32 a, float_status *s)
{
    return float32_to_int64_scalbn(a, s->float_rounding_mode, 0, s);
}

int16_t float64_to_int16(float64 a, float_status *s)
{
    return float64_to_int16_scalbn(a, s->float_rounding_mode, 0, s);
}

int32_t float64_to_int32(float64 a, float_status *s)
{
    return float64_to_int32_scalbn(a, s->float_rounding_mode, 0, s);
}

int64_t float64_to_int64(float64 a, float_status *s)
{
    return float64_to_int64_scalbn(a, s->float_rounding_mode, 0, s);
}

int16_t float16_to_int16_round_to_zero(float16 a, float_status *s)
{
    return float16_to_int16_scalbn(a, float_round_to_zero, 0, s);
}

int32_t float16_to_int32_round_to_zero(float16 a, float_status *s)
{
    return float16_to_int32_scalbn(a, float_round_to_zero, 0, s);
}

int64_t float16_to_int64_round_to_zero(float16 a, float_status *s)
{
    return float16_to_int64_scalbn(a, float_round_to_zero, 0, s);
}

int16_t float32_to_int16_round_to_zero(float32 a, float_status *s)
{
    return float32_to_int16_scalbn(a, float_round_to_zero, 0, s);
}

int32_t float32_to_int32_round_to_zero(float32 a, float_status *s)
{
    return float32_to_int32_scalbn(a, float_round_to_zero, 0, s);
}

int64_t float32_to_int64_round_to_zero(float32 a, float_status *s)
{
    return float32_to_int64_scalbn(a, float_round_to_zero, 0, s);
}

int16_t float64_to_int16_round_to_zero(float64 a, float_status *s)
{
    return float64_to_int16_scalbn(a, float_round_to_zero, 0, s);
}

int32_t float64_to_int32_round_to_zero(float64 a, float_status *s)
{
    return float64_to_int32_scalbn(a, float_round_to_zero, 0, s);
}

int64_t float64_to_int64_round_to_zero(float64 a, float_status *s)
{
    return float64_to_int64_scalbn(a, float_round_to_zero, 0, s);
}

/*
 *  Returns the result of converting the floating-point value `a' to
 *  the unsigned integer format. The conversion is performed according
 *  to the IEC/IEEE Standard for Binary Floating-Point
 *  Arithmetic---which means in particular that the conversion is
 *  rounded according to the current rounding mode. If `a' is a NaN,
 *  the largest unsigned integer is returned. Otherwise, if the
 *  conversion overflows, the largest unsigned integer is returned. If
 *  the 'a' is negative, the result is rounded and zero is returned;
 *  values that do not round to zero will raise the inexact exception
 *  flag.
 */

static uint64_t round_to_uint_and_pack(FloatParts in, int rmode, int scale,
                                       uint64_t max, float_status *s)
{
    int orig_flags = get_float_exception_flags(s);
    FloatParts p = round_to_int(in, rmode, scale, s);
    uint64_t r;

    switch (p.cls) {
    case float_class_snan:
    case float_class_qnan:
        s->float_exception_flags = orig_flags | float_flag_invalid;
        return max;
    case float_class_inf:
        s->float_exception_flags = orig_flags | float_flag_invalid;
        return p.sign ? 0 : max;
    case float_class_zero:
        return 0;
    case float_class_normal:
        if (p.sign) {
            s->float_exception_flags = orig_flags | float_flag_invalid;
            return 0;
        }

        if (p.exp < DECOMPOSED_BINARY_POINT) {
            r = p.frac >> (DECOMPOSED_BINARY_POINT - p.exp);
        } else if (p.exp - DECOMPOSED_BINARY_POINT < 2) {
            r = p.frac << (p.exp - DECOMPOSED_BINARY_POINT);
        } else {
            s->float_exception_flags = orig_flags | float_flag_invalid;
            return max;
        }

        /* For uint64 this will never trip, but if p.exp is too large
         * to shift a decomposed fraction we shall have exited via the
         * 3rd leg above.
         */
        if (r > max) {
            s->float_exception_flags = orig_flags | float_flag_invalid;
            return max;
        }
        return r;
    default:
        g_assert_not_reached();
    }
}

uint16_t float16_to_uint16_scalbn(float16 a, int rmode, int scale,
                                  float_status *s)
{
    return round_to_uint_and_pack(float16_unpack_canonical(a, s),
                                  rmode, scale, UINT16_MAX, s);
}

uint32_t float16_to_uint32_scalbn(float16 a, int rmode, int scale,
                                  float_status *s)
{
    return round_to_uint_and_pack(float16_unpack_canonical(a, s),
                                  rmode, scale, UINT32_MAX, s);
}

uint64_t float16_to_uint64_scalbn(float16 a, int rmode, int scale,
                                  float_status *s)
{
    return round_to_uint_and_pack(float16_unpack_canonical(a, s),
                                  rmode, scale, UINT64_MAX, s);
}

uint16_t float32_to_uint16_scalbn(float32 a, int rmode, int scale,
                                  float_status *s)
{
    return round_to_uint_and_pack(float32_unpack_canonical(a, s),
                                  rmode, scale, UINT16_MAX, s);
}

uint32_t float32_to_uint32_scalbn(float32 a, int rmode, int scale,
                                  float_status *s)
{
    return round_to_uint_and_pack(float32_unpack_canonical(a, s),
                                  rmode, scale, UINT32_MAX, s);
}

uint64_t float32_to_uint64_scalbn(float32 a, int rmode, int scale,
                                  float_status *s)
{
    return round_to_uint_and_pack(float32_unpack_canonical(a, s),
                                  rmode, scale, UINT64_MAX, s);
}

uint16_t float64_to_uint16_scalbn(float64 a, int rmode, int scale,
                                  float_status *s)
{
    return round_to_uint_and_pack(float64_unpack_canonical(a, s),
                                  rmode, scale, UINT16_MAX, s);
}

uint32_t float64_to_uint32_scalbn(float64 a, int rmode, int scale,
                                  float_status *s)
{
    return round_to_uint_and_pack(float64_unpack_canonical(a, s),
                                  rmode, scale, UINT32_MAX, s);
}

uint64_t float64_to_uint64_scalbn(float64 a, int rmode, int scale,
                                  float_status *s)
{
    return round_to_uint_and_pack(float64_unpack_canonical(a, s),
                                  rmode, scale, UINT64_MAX, s);
}

uint16_t float16_to_uint16(float16 a, float_status *s)
{
    return float16_to_uint16_scalbn(a, s->float_rounding_mode, 0, s);
}

uint32_t float16_to_uint32(float16 a, float_status *s)
{
    return float16_to_uint32_scalbn(a, s->float_rounding_mode, 0, s);
}

uint64_t float16_to_uint64(float16 a, float_status *s)
{
    return float16_to_uint64_scalbn(a, s->float_rounding_mode, 0, s);
}

uint16_t float32_to_uint16(float32 a, float_status *s)
{
    return float32_to_uint16_scalbn(a, s->float_rounding_mode, 0, s);
}

uint32_t float32_to_uint32(float32 a, float_status *s)
{
    return float32_to_uint32_scalbn(a, s->float_rounding_mode, 0, s);
}

uint64_t float32_to_uint64(float32 a, float_status *s)
{
    return float32_to_uint64_scalbn(a, s->float_rounding_mode, 0, s);
}

uint16_t float64_to_uint16(float64 a, float_status *s)
{
    return float64_to_uint16_scalbn(a, s->float_rounding_mode, 0, s);
}

uint32_t float64_to_uint32(float64 a, float_status *s)
{
    return float64_to_uint32_scalbn(a, s->float_rounding_mode, 0, s);
}

uint64_t float64_to_uint64(float64 a, float_status *s)
{
    return float64_to_uint64_scalbn(a, s->float_rounding_mode, 0, s);
}

uint16_t float16_to_uint16_round_to_zero(float16 a, float_status *s)
{
    return float16_to_uint16_scalbn(a, float_round_to_zero, 0, s);
}

uint32_t float16_to_uint32_round_to_zero(float16 a, float_status *s)
{
    return float16_to_uint32_scalbn(a, float_round_to_zero, 0, s);
}

uint64_t float16_to_uint64_round_to_zero(float16 a, float_status *s)
{
    return float16_to_uint64_scalbn(a, float_round_to_zero, 0, s);
}

uint16_t float32_to_uint16_round_to_zero(float32 a, float_status *s)
{
    return float32_to_uint16_scalbn(a, float_round_to_zero, 0, s);
}

uint32_t float32_to_uint32_round_to_zero(float32 a, float_status *s)
{
    return float32_to_uint32_scalbn(a, float_round_to_zero, 0, s);
}

uint64_t float32_to_uint64_round_to_zero(float32 a, float_status *s)
{
    return float32_to_uint64_scalbn(a, float_round_to_zero, 0, s);
}

uint16_t float64_to_uint16_round_to_zero(float64 a, float_status *s)
{
    return float64_to_uint16_scalbn(a, float_round_to_zero, 0, s);
}

uint32_t float64_to_uint32_round_to_zero(float64 a, float_status *s)
{
    return float64_to_uint32_scalbn(a, float_round_to_zero, 0, s);
}

uint64_t float64_to_uint64_round_to_zero(float64 a, float_status *s)
{
    return float64_to_uint64_scalbn(a, float_round_to_zero, 0, s);
}

/*
 * Integer to float conversions
 *
 * Returns the result of converting the two's complement integer `a'
 * to the floating-point format. The conversion is performed according
 * to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
 */

static FloatParts int_to_float(int64_t a, int scale, float_status *status)
{
    FloatParts r = { .sign = false };

    if (a == 0) {
        r.cls = float_class_zero;
    } else {
        uint64_t f = a;
        int shift;

        r.cls = float_class_normal;
        if (a < 0) {
            f = -f;
            r.sign = true;
        }
        shift = clz64(f) - 1;
        scale = MIN(MAX(scale, -0x10000), 0x10000);

        r.exp = DECOMPOSED_BINARY_POINT - shift + scale;
        r.frac = (shift < 0 ? DECOMPOSED_IMPLICIT_BIT : f << shift);
    }

    return r;
}

float16 int64_to_float16_scalbn(int64_t a, int scale, float_status *status)
{
    FloatParts pa = int_to_float(a, scale, status);
    return float16_round_pack_canonical(pa, status);
}

float16 int32_to_float16_scalbn(int32_t a, int scale, float_status *status)
{
    return int64_to_float16_scalbn(a, scale, status);
}

float16 int16_to_float16_scalbn(int16_t a, int scale, float_status *status)
{
    return int64_to_float16_scalbn(a, scale, status);
}

float16 int64_to_float16(int64_t a, float_status *status)
{
    return int64_to_float16_scalbn(a, 0, status);
}

float16 int32_to_float16(int32_t a, float_status *status)
{
    return int64_to_float16_scalbn(a, 0, status);
}

float16 int16_to_float16(int16_t a, float_status *status)
{
    return int64_to_float16_scalbn(a, 0, status);
}

float32 int64_to_float32_scalbn(int64_t a, int scale, float_status *status)
{
    FloatParts pa = int_to_float(a, scale, status);
    return float32_round_pack_canonical(pa, status);
}

float32 int32_to_float32_scalbn(int32_t a, int scale, float_status *status)
{
    return int64_to_float32_scalbn(a, scale, status);
}

float32 int16_to_float32_scalbn(int16_t a, int scale, float_status *status)
{
    return int64_to_float32_scalbn(a, scale, status);
}

float32 int64_to_float32(int64_t a, float_status *status)
{
    return int64_to_float32_scalbn(a, 0, status);
}

float32 int32_to_float32(int32_t a, float_status *status)
{
    return int64_to_float32_scalbn(a, 0, status);
}

float32 int16_to_float32(int16_t a, float_status *status)
{
    return int64_to_float32_scalbn(a, 0, status);
}

float64 int64_to_float64_scalbn(int64_t a, int scale, float_status *status)
{
    FloatParts pa = int_to_float(a, scale, status);
    return float64_round_pack_canonical(pa, status);
}

float64 int32_to_float64_scalbn(int32_t a, int scale, float_status *status)
{
    return int64_to_float64_scalbn(a, scale, status);
}

float64 int16_to_float64_scalbn(int16_t a, int scale, float_status *status)
{
    return int64_to_float64_scalbn(a, scale, status);
}

float64 int64_to_float64(int64_t a, float_status *status)
{
    return int64_to_float64_scalbn(a, 0, status);
}

float64 int32_to_float64(int32_t a, float_status *status)
{
    return int64_to_float64_scalbn(a, 0, status);
}

float64 int16_to_float64(int16_t a, float_status *status)
{
    return int64_to_float64_scalbn(a, 0, status);
}


/*
 * Unsigned Integer to float conversions
 *
 * Returns the result of converting the unsigned integer `a' to the
 * floating-point format. The conversion is performed according to the
 * IEC/IEEE Standard for Binary Floating-Point Arithmetic.
 */

static FloatParts uint_to_float(uint64_t a, int scale, float_status *status)
{
    FloatParts r = { .sign = false };

    if (a == 0) {
        r.cls = float_class_zero;
    } else {
        scale = MIN(MAX(scale, -0x10000), 0x10000);
        r.cls = float_class_normal;
        if ((int64_t)a < 0) {
            r.exp = DECOMPOSED_BINARY_POINT + 1 + scale;
            shift64RightJamming(a, 1, &a);
            r.frac = a;
        } else {
            int shift = clz64(a) - 1;
            r.exp = DECOMPOSED_BINARY_POINT - shift + scale;
            r.frac = a << shift;
        }
    }

    return r;
}

float16 uint64_to_float16_scalbn(uint64_t a, int scale, float_status *status)
{
    FloatParts pa = uint_to_float(a, scale, status);
    return float16_round_pack_canonical(pa, status);
}

float16 uint32_to_float16_scalbn(uint32_t a, int scale, float_status *status)
{
    return uint64_to_float16_scalbn(a, scale, status);
}

float16 uint16_to_float16_scalbn(uint16_t a, int scale, float_status *status)
{
    return uint64_to_float16_scalbn(a, scale, status);
}

float16 uint64_to_float16(uint64_t a, float_status *status)
{
    return uint64_to_float16_scalbn(a, 0, status);
}

float16 uint32_to_float16(uint32_t a, float_status *status)
{
    return uint64_to_float16_scalbn(a, 0, status);
}

float16 uint16_to_float16(uint16_t a, float_status *status)
{
    return uint64_to_float16_scalbn(a, 0, status);
}

float32 uint64_to_float32_scalbn(uint64_t a, int scale, float_status *status)
{
    FloatParts pa = uint_to_float(a, scale, status);
    return float32_round_pack_canonical(pa, status);
}

float32 uint32_to_float32_scalbn(uint32_t a, int scale, float_status *status)
{
    return uint64_to_float32_scalbn(a, scale, status);
}

float32 uint16_to_float32_scalbn(uint16_t a, int scale, float_status *status)
{
    return uint64_to_float32_scalbn(a, scale, status);
}

float32 uint64_to_float32(uint64_t a, float_status *status)
{
    return uint64_to_float32_scalbn(a, 0, status);
}

float32 uint32_to_float32(uint32_t a, float_status *status)
{
    return uint64_to_float32_scalbn(a, 0, status);
}

float32 uint16_to_float32(uint16_t a, float_status *status)
{
    return uint64_to_float32_scalbn(a, 0, status);
}

float64 uint64_to_float64_scalbn(uint64_t a, int scale, float_status *status)
{
    FloatParts pa = uint_to_float(a, scale, status);
    return float64_round_pack_canonical(pa, status);
}

float64 uint32_to_float64_scalbn(uint32_t a, int scale, float_status *status)
{
    return uint64_to_float64_scalbn(a, scale, status);
}

float64 uint16_to_float64_scalbn(uint16_t a, int scale, float_status *status)
{
    return uint64_to_float64_scalbn(a, scale, status);
}

float64 uint64_to_float64(uint64_t a, float_status *status)
{
    return uint64_to_float64_scalbn(a, 0, status);
}

float64 uint32_to_float64(uint32_t a, float_status *status)
{
    return uint64_to_float64_scalbn(a, 0, status);
}

float64 uint16_to_float64(uint16_t a, float_status *status)
{
    return uint64_to_float64_scalbn(a, 0, status);
}

/* Float Min/Max */
/* min() and max() functions. These can't be implemented as
 * 'compare and pick one input' because that would mishandle
 * NaNs and +0 vs -0.
 *
 * minnum() and maxnum() functions. These are similar to the min()
 * and max() functions but if one of the arguments is a QNaN and
 * the other is numerical then the numerical argument is returned.
 * SNaNs will get quietened before being returned.
 * minnum() and maxnum correspond to the IEEE 754-2008 minNum()
 * and maxNum() operations. min() and max() are the typical min/max
 * semantics provided by many CPUs which predate that specification.
 *
 * minnummag() and maxnummag() functions correspond to minNumMag()
 * and minNumMag() from the IEEE-754 2008.
 */
static FloatParts minmax_floats(FloatParts a, FloatParts b, bool ismin,
                                bool ieee, bool ismag, float_status *s)
{
    if (unlikely(is_nan(a.cls) || is_nan(b.cls))) {
        if (ieee) {
            /* Takes two floating-point values `a' and `b', one of
             * which is a NaN, and returns the appropriate NaN
             * result. If either `a' or `b' is a signaling NaN,
             * the invalid exception is raised.
             */
            if (is_snan(a.cls) || is_snan(b.cls)) {
                return pick_nan(a, b, s);
            } else if (is_nan(a.cls) && !is_nan(b.cls)) {
                return b;
            } else if (is_nan(b.cls) && !is_nan(a.cls)) {
                return a;
            }
        }
        return pick_nan(a, b, s);
    } else {
        int a_exp, b_exp;

        switch (a.cls) {
        case float_class_normal:
            a_exp = a.exp;
            break;
        case float_class_inf:
            a_exp = INT_MAX;
            break;
        case float_class_zero:
            a_exp = INT_MIN;
            break;
        default:
            g_assert_not_reached();
            break;
        }
        switch (b.cls) {
        case float_class_normal:
            b_exp = b.exp;
            break;
        case float_class_inf:
            b_exp = INT_MAX;
            break;
        case float_class_zero:
            b_exp = INT_MIN;
            break;
        default:
            g_assert_not_reached();
            break;
        }

        if (ismag && (a_exp != b_exp || a.frac != b.frac)) {
            bool a_less = a_exp < b_exp;
            if (a_exp == b_exp) {
                a_less = a.frac < b.frac;
            }
            return a_less ^ ismin ? b : a;
        }

        if (a.sign == b.sign) {
            bool a_less = a_exp < b_exp;
            if (a_exp == b_exp) {
                a_less = a.frac < b.frac;
            }
            return a.sign ^ a_less ^ ismin ? b : a;
        } else {
            return a.sign ^ ismin ? b : a;
        }
    }
}

#define MINMAX(sz, name, ismin, isiee, ismag)                           \
float ## sz float ## sz ## _ ## name(float ## sz a, float ## sz b,      \
                                     float_status *s)                   \
{                                                                       \
    FloatParts pa = float ## sz ## _unpack_canonical(a, s);             \
    FloatParts pb = float ## sz ## _unpack_canonical(b, s);             \
    FloatParts pr = minmax_floats(pa, pb, ismin, isiee, ismag, s);      \
                                                                        \
    return float ## sz ## _round_pack_canonical(pr, s);                 \
}

MINMAX(16, min, true, false, false)
MINMAX(16, minnum, true, true, false)
MINMAX(16, minnummag, true, true, true)
MINMAX(16, max, false, false, false)
MINMAX(16, maxnum, false, true, false)
MINMAX(16, maxnummag, false, true, true)

MINMAX(32, min, true, false, false)
MINMAX(32, minnum, true, true, false)
MINMAX(32, minnummag, true, true, true)
MINMAX(32, max, false, false, false)
MINMAX(32, maxnum, false, true, false)
MINMAX(32, maxnummag, false, true, true)

MINMAX(64, min, true, false, false)
MINMAX(64, minnum, true, true, false)
MINMAX(64, minnummag, true, true, true)
MINMAX(64, max, false, false, false)
MINMAX(64, maxnum, false, true, false)
MINMAX(64, maxnummag, false, true, true)

#undef MINMAX

/* Floating point compare */
static int compare_floats(FloatParts a, FloatParts b, bool is_quiet,
                          float_status *s)
{
    if (is_nan(a.cls) || is_nan(b.cls)) {
        if (!is_quiet ||
            a.cls == float_class_snan ||
            b.cls == float_class_snan) {
            s->float_exception_flags |= float_flag_invalid;
        }
        return float_relation_unordered;
    }

    if (a.cls == float_class_zero) {
        if (b.cls == float_class_zero) {
            return float_relation_equal;
        }
        return b.sign ? float_relation_greater : float_relation_less;
    } else if (b.cls == float_class_zero) {
        return a.sign ? float_relation_less : float_relation_greater;
    }

    /* The only really important thing about infinity is its sign. If
     * both are infinities the sign marks the smallest of the two.
     */
    if (a.cls == float_class_inf) {
        if ((b.cls == float_class_inf) && (a.sign == b.sign)) {
            return float_relation_equal;
        }
        return a.sign ? float_relation_less : float_relation_greater;
    } else if (b.cls == float_class_inf) {
        return b.sign ? float_relation_greater : float_relation_less;
    }

    if (a.sign != b.sign) {
        return a.sign ? float_relation_less : float_relation_greater;
    }

    if (a.exp == b.exp) {
        if (a.frac == b.frac) {
            return float_relation_equal;
        }
        if (a.sign) {
            return a.frac > b.frac ?
                float_relation_less : float_relation_greater;
        } else {
            return a.frac > b.frac ?
                float_relation_greater : float_relation_less;
        }
    } else {
        if (a.sign) {
            return a.exp > b.exp ? float_relation_less : float_relation_greater;
        } else {
            return a.exp > b.exp ? float_relation_greater : float_relation_less;
        }
    }
}

#define COMPARE(name, attr, sz)                                         \
static int attr                                                         \
name(float ## sz a, float ## sz b, bool is_quiet, float_status *s)      \
{                                                                       \
    FloatParts pa = float ## sz ## _unpack_canonical(a, s);             \
    FloatParts pb = float ## sz ## _unpack_canonical(b, s);             \
    return compare_floats(pa, pb, is_quiet, s);                         \
}

COMPARE(soft_f16_compare, QEMU_FLATTEN, 16)
COMPARE(soft_f32_compare, QEMU_SOFTFLOAT_ATTR, 32)
COMPARE(soft_f64_compare, QEMU_SOFTFLOAT_ATTR, 64)

#undef COMPARE

int float16_compare(float16 a, float16 b, float_status *s)
{
    return soft_f16_compare(a, b, false, s);
}

int float16_compare_quiet(float16 a, float16 b, float_status *s)
{
    return soft_f16_compare(a, b, true, s);
}

static int QEMU_FLATTEN
f32_compare(float32 xa, float32 xb, bool is_quiet, float_status *s)
{
    union_float32 ua, ub;

    ua.s = xa;
    ub.s = xb;

    if (QEMU_NO_HARDFLOAT) {
        goto soft;
    }

    float32_input_flush2(&ua.s, &ub.s, s);
    if (isgreaterequal(ua.h, ub.h)) {
        if (isgreater(ua.h, ub.h)) {
            return float_relation_greater;
        }
        return float_relation_equal;
    }
    if (likely(isless(ua.h, ub.h))) {
        return float_relation_less;
    }
    /* The only condition remaining is unordered.
     * Fall through to set flags.
     */
 soft:
    return soft_f32_compare(ua.s, ub.s, is_quiet, s);
}

int float32_compare(float32 a, float32 b, float_status *s)
{
    return f32_compare(a, b, false, s);
}

int float32_compare_quiet(float32 a, float32 b, float_status *s)
{
    return f32_compare(a, b, true, s);
}

static int QEMU_FLATTEN
f64_compare(float64 xa, float64 xb, bool is_quiet, float_status *s)
{
    union_float64 ua, ub;

    ua.s = xa;
    ub.s = xb;

    if (QEMU_NO_HARDFLOAT) {
        goto soft;
    }

    float64_input_flush2(&ua.s, &ub.s, s);
    if (isgreaterequal(ua.h, ub.h)) {
        if (isgreater(ua.h, ub.h)) {
            return float_relation_greater;
        }
        return float_relation_equal;
    }
    if (likely(isless(ua.h, ub.h))) {
        return float_relation_less;
    }
    /* The only condition remaining is unordered.
     * Fall through to set flags.
     */
 soft:
    return soft_f64_compare(ua.s, ub.s, is_quiet, s);
}

int float64_compare(float64 a, float64 b, float_status *s)
{
    return f64_compare(a, b, false, s);
}

int float64_compare_quiet(float64 a, float64 b, float_status *s)
{
    return f64_compare(a, b, true, s);
}

/* Multiply A by 2 raised to the power N.  */
static FloatParts scalbn_decomposed(FloatParts a, int n, float_status *s)
{
    if (unlikely(is_nan(a.cls))) {
        return return_nan(a, s);
    }
    if (a.cls == float_class_normal) {
        /* The largest float type (even though not supported by FloatParts)
         * is float128, which has a 15 bit exponent.  Bounding N to 16 bits
         * still allows rounding to infinity, without allowing overflow
         * within the int32_t that backs FloatParts.exp.
         */
        n = MIN(MAX(n, -0x10000), 0x10000);
        a.exp += n;
    }
    return a;
}

float16 float16_scalbn(float16 a, int n, float_status *status)
{
    FloatParts pa = float16_unpack_canonical(a, status);
    FloatParts pr = scalbn_decomposed(pa, n, status);
    return float16_round_pack_canonical(pr, status);
}

float32 float32_scalbn(float32 a, int n, float_status *status)
{
    FloatParts pa = float32_unpack_canonical(a, status);
    FloatParts pr = scalbn_decomposed(pa, n, status);
    return float32_round_pack_canonical(pr, status);
}

float64 float64_scalbn(float64 a, int n, float_status *status)
{
    FloatParts pa = float64_unpack_canonical(a, status);
    FloatParts pr = scalbn_decomposed(pa, n, status);
    return float64_round_pack_canonical(pr, status);
}

/*
 * Square Root
 *
 * The old softfloat code did an approximation step before zeroing in
 * on the final result. However for simpleness we just compute the
 * square root by iterating down from the implicit bit to enough extra
 * bits to ensure we get a correctly rounded result.
 *
 * This does mean however the calculation is slower than before,
 * especially for 64 bit floats.
 */

static FloatParts sqrt_float(FloatParts a, float_status *s, const FloatFmt *p)
{
    uint64_t a_frac, r_frac, s_frac;
    int bit, last_bit;

    if (is_nan(a.cls)) {
        return return_nan(a, s);
    }
    if (a.cls == float_class_zero) {
        return a;  /* sqrt(+-0) = +-0 */
    }
    if (a.sign) {
        s->float_exception_flags |= float_flag_invalid;
        return parts_default_nan(s);
    }
    if (a.cls == float_class_inf) {
        return a;  /* sqrt(+inf) = +inf */
    }

    assert(a.cls == float_class_normal);

    /* We need two overflow bits at the top. Adding room for that is a
     * right shift. If the exponent is odd, we can discard the low bit
     * by multiplying the fraction by 2; that's a left shift. Combine
     * those and we shift right if the exponent is even.
     */
    a_frac = a.frac;
    if (!(a.exp & 1)) {
        a_frac >>= 1;
    }
    a.exp >>= 1;

    /* Bit-by-bit computation of sqrt.  */
    r_frac = 0;
    s_frac = 0;

    /* Iterate from implicit bit down to the 3 extra bits to compute a
     * properly rounded result. Remember we've inserted one more bit
     * at the top, so these positions are one less.
     */
    bit = DECOMPOSED_BINARY_POINT - 1;
    last_bit = MAX(p->frac_shift - 4, 0);
    do {
        uint64_t q = 1ULL << bit;
        uint64_t t_frac = s_frac + q;
        if (t_frac <= a_frac) {
            s_frac = t_frac + q;
            a_frac -= t_frac;
            r_frac += q;
        }
        a_frac <<= 1;
    } while (--bit >= last_bit);

    /* Undo the right shift done above. If there is any remaining
     * fraction, the result is inexact. Set the sticky bit.
     */
    a.frac = (r_frac << 1) + (a_frac != 0);

    return a;
}

float16 QEMU_FLATTEN float16_sqrt(float16 a, float_status *status)
{
    FloatParts pa = float16_unpack_canonical(a, status);
    FloatParts pr = sqrt_float(pa, status, &float16_params);
    return float16_round_pack_canonical(pr, status);
}

static float32 QEMU_SOFTFLOAT_ATTR
soft_f32_sqrt(float32 a, float_status *status)
{
    FloatParts pa = float32_unpack_canonical(a, status);
    FloatParts pr = sqrt_float(pa, status, &float32_params);
    return float32_round_pack_canonical(pr, status);
}

static float64 QEMU_SOFTFLOAT_ATTR
soft_f64_sqrt(float64 a, float_status *status)
{
    FloatParts pa = float64_unpack_canonical(a, status);
    FloatParts pr = sqrt_float(pa, status, &float64_params);
    return float64_round_pack_canonical(pr, status);
}

float32 QEMU_FLATTEN float32_sqrt(float32 xa, float_status *s)
{
    union_float32 ua, ur;

    ua.s = xa;
    if (unlikely(!can_use_fpu(s))) {
        goto soft;
    }

    float32_input_flush1(&ua.s, s);
    if (QEMU_HARDFLOAT_1F32_USE_FP) {
        if (unlikely(!(fpclassify(ua.h) == FP_NORMAL ||
                       fpclassify(ua.h) == FP_ZERO) ||
                     signbit(ua.h))) {
            goto soft;
        }
    } else if (unlikely(!float32_is_zero_or_normal(ua.s) ||
                        float32_is_neg(ua.s))) {
        goto soft;
    }
    ur.h = sqrtf(ua.h);
    return ur.s;

 soft:
    return soft_f32_sqrt(ua.s, s);
}

float64 QEMU_FLATTEN float64_sqrt(float64 xa, float_status *s)
{
    union_float64 ua, ur;

    ua.s = xa;
    if (unlikely(!can_use_fpu(s))) {
        goto soft;
    }

    float64_input_flush1(&ua.s, s);
    if (QEMU_HARDFLOAT_1F64_USE_FP) {
        if (unlikely(!(fpclassify(ua.h) == FP_NORMAL ||
                       fpclassify(ua.h) == FP_ZERO) ||
                     signbit(ua.h))) {
            goto soft;
        }
    } else if (unlikely(!float64_is_zero_or_normal(ua.s) ||
                        float64_is_neg(ua.s))) {
        goto soft;
    }
    ur.h = sqrt(ua.h);
    return ur.s;

 soft:
    return soft_f64_sqrt(ua.s, s);
}

/*----------------------------------------------------------------------------
| The pattern for a default generated NaN.
*----------------------------------------------------------------------------*/

float16 float16_default_nan(float_status *status)
{
    FloatParts p = parts_default_nan(status);
    p.frac >>= float16_params.frac_shift;
    return float16_pack_raw(p);
}

float32 float32_default_nan(float_status *status)
{
    FloatParts p = parts_default_nan(status);
    p.frac >>= float32_params.frac_shift;
    return float32_pack_raw(p);
}

float64 float64_default_nan(float_status *status)
{
    FloatParts p = parts_default_nan(status);
    p.frac >>= float64_params.frac_shift;
    return float64_pack_raw(p);
}

float128 float128_default_nan(float_status *status)
{
    FloatParts p = parts_default_nan(status);
    float128 r;

    /* Extrapolate from the choices made by parts_default_nan to fill
     * in the quad-floating format.  If the low bit is set, assume we
     * want to set all non-snan bits.
     */
    r.low = -(p.frac & 1);
    r.high = p.frac >> (DECOMPOSED_BINARY_POINT - 48);
    r.high |= UINT64_C(0x7FFF000000000000);
    r.high |= (uint64_t)p.sign << 63;

    return r;
}

/*----------------------------------------------------------------------------
| Returns a quiet NaN from a signalling NaN for the floating point value `a'.
*----------------------------------------------------------------------------*/

float16 float16_silence_nan(float16 a, float_status *status)
{
    FloatParts p = float16_unpack_raw(a);
    p.frac <<= float16_params.frac_shift;
    p = parts_silence_nan(p, status);
    p.frac >>= float16_params.frac_shift;
    return float16_pack_raw(p);
}

float32 float32_silence_nan(float32 a, float_status *status)
{
    FloatParts p = float32_unpack_raw(a);
    p.frac <<= float32_params.frac_shift;
    p = parts_silence_nan(p, status);
    p.frac >>= float32_params.frac_shift;
    return float32_pack_raw(p);
}

float64 float64_silence_nan(float64 a, float_status *status)
{
    FloatParts p = float64_unpack_raw(a);
    p.frac <<= float64_params.frac_shift;
    p = parts_silence_nan(p, status);
    p.frac >>= float64_params.frac_shift;
    return float64_pack_raw(p);
}


/*----------------------------------------------------------------------------
| If `a' is denormal and we are in flush-to-zero mode then set the
| input-denormal exception and return zero. Otherwise just return the value.
*----------------------------------------------------------------------------*/

static bool parts_squash_denormal(FloatParts p, float_status *status)
{
    if (p.exp == 0 && p.frac != 0) {
        float_raise(float_flag_input_denormal, status);
        return true;
    }

    return false;
}

float16 float16_squash_input_denormal(float16 a, float_status *status)
{
    if (status->flush_inputs_to_zero) {
        FloatParts p = float16_unpack_raw(a);
        if (parts_squash_denormal(p, status)) {
            return float16_set_sign(float16_zero, p.sign);
        }
    }
    return a;
}

float32 float32_squash_input_denormal(float32 a, float_status *status)
{
    if (status->flush_inputs_to_zero) {
        FloatParts p = float32_unpack_raw(a);
        if (parts_squash_denormal(p, status)) {
            return float32_set_sign(float32_zero, p.sign);
        }
    }
    return a;
}

float64 float64_squash_input_denormal(float64 a, float_status *status)
{
    if (status->flush_inputs_to_zero) {
        FloatParts p = float64_unpack_raw(a);
        if (parts_squash_denormal(p, status)) {
            return float64_set_sign(float64_zero, p.sign);
        }
    }
    return a;
}

/*----------------------------------------------------------------------------
| Takes a 64-bit fixed-point value `absZ' with binary point between bits 6
| and 7, and returns the properly rounded 32-bit integer corresponding to the
| input.  If `zSign' is 1, the input is negated before being converted to an
| integer.  Bit 63 of `absZ' must be zero.  Ordinarily, the fixed-point input
| is simply rounded to an integer, with the inexact exception raised if the
| input cannot be represented exactly as an integer.  However, if the fixed-
| point input is too large, the invalid exception is raised and the largest
| positive or negative integer is returned.
*----------------------------------------------------------------------------*/

static int32_t roundAndPackInt32(flag zSign, uint64_t absZ, float_status *status)
{
    int8_t roundingMode;
    flag roundNearestEven;
    int8_t roundIncrement, roundBits;
    int32_t z;

    roundingMode = status->float_rounding_mode;
    roundNearestEven = ( roundingMode == float_round_nearest_even );
    switch (roundingMode) {
    case float_round_nearest_even:
    case float_round_ties_away:
        roundIncrement = 0x40;
        break;
    case float_round_to_zero:
        roundIncrement = 0;
        break;
    case float_round_up:
        roundIncrement = zSign ? 0 : 0x7f;
        break;
    case float_round_down:
        roundIncrement = zSign ? 0x7f : 0;
        break;
    case float_round_to_odd:
        roundIncrement = absZ & 0x80 ? 0 : 0x7f;
        break;
    default:
        abort();
    }
    roundBits = absZ & 0x7F;
    absZ = ( absZ + roundIncrement )>>7;
    absZ &= ~ ( ( ( roundBits ^ 0x40 ) == 0 ) & roundNearestEven );
    z = absZ;
    if ( zSign ) z = - z;
    if ( ( absZ>>32 ) || ( z && ( ( z < 0 ) ^ zSign ) ) ) {
        float_raise(float_flag_invalid, status);
        return zSign ? INT32_MIN : INT32_MAX;
    }
    if (roundBits) {
        status->float_exception_flags |= float_flag_inexact;
    }
    return z;

}

/*----------------------------------------------------------------------------
| Takes the 128-bit fixed-point value formed by concatenating `absZ0' and
| `absZ1', with binary point between bits 63 and 64 (between the input words),
| and returns the properly rounded 64-bit integer corresponding to the input.
| If `zSign' is 1, the input is negated before being converted to an integer.
| Ordinarily, the fixed-point input is simply rounded to an integer, with
| the inexact exception raised if the input cannot be represented exactly as
| an integer.  However, if the fixed-point input is too large, the invalid
| exception is raised and the largest positive or negative integer is
| returned.
*----------------------------------------------------------------------------*/

static int64_t roundAndPackInt64(flag zSign, uint64_t absZ0, uint64_t absZ1,
                               float_status *status)
{
    int8_t roundingMode;
    flag roundNearestEven, increment;
    int64_t z;

    roundingMode = status->float_rounding_mode;
    roundNearestEven = ( roundingMode == float_round_nearest_even );
    switch (roundingMode) {
    case float_round_nearest_even:
    case float_round_ties_away:
        increment = ((int64_t) absZ1 < 0);
        break;
    case float_round_to_zero:
        increment = 0;
        break;
    case float_round_up:
        increment = !zSign && absZ1;
        break;
    case float_round_down:
        increment = zSign && absZ1;
        break;
    case float_round_to_odd:
        increment = !(absZ0 & 1) && absZ1;
        break;
    default:
        abort();
    }
    if ( increment ) {
        ++absZ0;
        if ( absZ0 == 0 ) goto overflow;
        absZ0 &= ~ ( ( (uint64_t) ( absZ1<<1 ) == 0 ) & roundNearestEven );
    }
    z = absZ0;
    if ( zSign ) z = - z;
    if ( z && ( ( z < 0 ) ^ zSign ) ) {
 overflow:
        float_raise(float_flag_invalid, status);
        return zSign ? INT64_MIN : INT64_MAX;
    }
    if (absZ1) {
        status->float_exception_flags |= float_flag_inexact;
    }
    return z;

}

/*----------------------------------------------------------------------------
| Takes the 128-bit fixed-point value formed by concatenating `absZ0' and
| `absZ1', with binary point between bits 63 and 64 (between the input words),
| and returns the properly rounded 64-bit unsigned integer corresponding to the
| input.  Ordinarily, the fixed-point input is simply rounded to an integer,
| with the inexact exception raised if the input cannot be represented exactly
| as an integer.  However, if the fixed-point input is too large, the invalid
| exception is raised and the largest unsigned integer is returned.
*----------------------------------------------------------------------------*/

static int64_t roundAndPackUint64(flag zSign, uint64_t absZ0,
                                uint64_t absZ1, float_status *status)
{
    int8_t roundingMode;
    flag roundNearestEven, increment;

    roundingMode = status->float_rounding_mode;
    roundNearestEven = (roundingMode == float_round_nearest_even);
    switch (roundingMode) {
    case float_round_nearest_even:
    case float_round_ties_away:
        increment = ((int64_t)absZ1 < 0);
        break;
    case float_round_to_zero:
        increment = 0;
        break;
    case float_round_up:
        increment = !zSign && absZ1;
        break;
    case float_round_down:
        increment = zSign && absZ1;
        break;
    case float_round_to_odd:
        increment = !(absZ0 & 1) && absZ1;
        break;
    default:
        abort();
    }
    if (increment) {
        ++absZ0;
        if (absZ0 == 0) {
            float_raise(float_flag_invalid, status);
            return UINT64_MAX;
        }
        absZ0 &= ~(((uint64_t)(absZ1<<1) == 0) & roundNearestEven);
    }

    if (zSign && absZ0) {
        float_raise(float_flag_invalid, status);
        return 0;
    }

    if (absZ1) {
        status->float_exception_flags |= float_flag_inexact;
    }
    return absZ0;
}

/*----------------------------------------------------------------------------
| Normalizes the subnormal single-precision floating-point value represented
| by the denormalized significand `aSig'.  The normalized exponent and
| significand are stored at the locations pointed to by `zExpPtr' and
| `zSigPtr', respectively.
*----------------------------------------------------------------------------*/

static void
 normalizeFloat32Subnormal(uint32_t aSig, int *zExpPtr, uint32_t *zSigPtr)
{
    int8_t shiftCount;

    shiftCount = clz32(aSig) - 8;
    *zSigPtr = aSig<<shiftCount;
    *zExpPtr = 1 - shiftCount;

}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and significand `zSig', and returns the proper single-precision floating-
| point value corresponding to the abstract input.  Ordinarily, the abstract
| value is simply rounded and packed into the single-precision format, with
| the inexact exception raised if the abstract input cannot be represented
| exactly.  However, if the abstract value is too large, the overflow and
| inexact exceptions are raised and an infinity or maximal finite value is
| returned.  If the abstract value is too small, the input value is rounded to
| a subnormal number, and the underflow and inexact exceptions are raised if
| the abstract input cannot be represented exactly as a subnormal single-
| precision floating-point number.
|     The input significand `zSig' has its binary point between bits 30
| and 29, which is 7 bits to the left of the usual location.  This shifted
| significand must be normalized or smaller.  If `zSig' is not normalized,
| `zExp' must be 0; in that case, the result returned is a subnormal number,
| and it must not require rounding.  In the usual case that `zSig' is
| normalized, `zExp' must be 1 less than the ``true'' floating-point exponent.
| The handling of underflow and overflow follows the IEC/IEEE Standard for
| Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static float32 roundAndPackFloat32(flag zSign, int zExp, uint32_t zSig,
                                   float_status *status)
{
    int8_t roundingMode;
    flag roundNearestEven;
    int8_t roundIncrement, roundBits;
    flag isTiny;

    roundingMode = status->float_rounding_mode;
    roundNearestEven = ( roundingMode == float_round_nearest_even );
    switch (roundingMode) {
    case float_round_nearest_even:
    case float_round_ties_away:
        roundIncrement = 0x40;
        break;
    case float_round_to_zero:
        roundIncrement = 0;
        break;
    case float_round_up:
        roundIncrement = zSign ? 0 : 0x7f;
        break;
    case float_round_down:
        roundIncrement = zSign ? 0x7f : 0;
        break;
    case float_round_to_odd:
        roundIncrement = zSig & 0x80 ? 0 : 0x7f;
        break;
    default:
        abort();
        break;
    }
    roundBits = zSig & 0x7F;
    if ( 0xFD <= (uint16_t) zExp ) {
        if (    ( 0xFD < zExp )
             || (    ( zExp == 0xFD )
                  && ( (int32_t) ( zSig + roundIncrement ) < 0 ) )
           ) {
            bool overflow_to_inf = roundingMode != float_round_to_odd &&
                                   roundIncrement != 0;
            float_raise(float_flag_overflow | float_flag_inexact, status);
            return packFloat32(zSign, 0xFF, -!overflow_to_inf);
        }
        if ( zExp < 0 ) {
            if (status->flush_to_zero) {
                float_raise(float_flag_output_denormal, status);
                return packFloat32(zSign, 0, 0);
            }
            isTiny =
                (status->float_detect_tininess
                 == float_tininess_before_rounding)
                || ( zExp < -1 )
                || ( zSig + roundIncrement < 0x80000000 );
            shift32RightJamming( zSig, - zExp, &zSig );
            zExp = 0;
            roundBits = zSig & 0x7F;
            if (isTiny && roundBits) {
                float_raise(float_flag_underflow, status);
            }
            if (roundingMode == float_round_to_odd) {
                /*
                 * For round-to-odd case, the roundIncrement depends on
                 * zSig which just changed.
                 */
                roundIncrement = zSig & 0x80 ? 0 : 0x7f;
            }
        }
    }
    if (roundBits) {
        status->float_exception_flags |= float_flag_inexact;
    }
    zSig = ( zSig + roundIncrement )>>7;
    zSig &= ~ ( ( ( roundBits ^ 0x40 ) == 0 ) & roundNearestEven );
    if ( zSig == 0 ) zExp = 0;
    return packFloat32( zSign, zExp, zSig );

}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and significand `zSig', and returns the proper single-precision floating-
| point value corresponding to the abstract input.  This routine is just like
| `roundAndPackFloat32' except that `zSig' does not have to be normalized.
| Bit 31 of `zSig' must be zero, and `zExp' must be 1 less than the ``true''
| floating-point exponent.
*----------------------------------------------------------------------------*/

static float32
 normalizeRoundAndPackFloat32(flag zSign, int zExp, uint32_t zSig,
                              float_status *status)
{
    int8_t shiftCount;

    shiftCount = clz32(zSig) - 1;
    return roundAndPackFloat32(zSign, zExp - shiftCount, zSig<<shiftCount,
                               status);

}

/*----------------------------------------------------------------------------
| Normalizes the subnormal double-precision floating-point value represented
| by the denormalized significand `aSig'.  The normalized exponent and
| significand are stored at the locations pointed to by `zExpPtr' and
| `zSigPtr', respectively.
*----------------------------------------------------------------------------*/

static void
 normalizeFloat64Subnormal(uint64_t aSig, int *zExpPtr, uint64_t *zSigPtr)
{
    int8_t shiftCount;

    shiftCount = clz64(aSig) - 11;
    *zSigPtr = aSig<<shiftCount;
    *zExpPtr = 1 - shiftCount;

}

/*----------------------------------------------------------------------------
| Packs the sign `zSign', exponent `zExp', and significand `zSig' into a
| double-precision floating-point value, returning the result.  After being
| shifted into the proper positions, the three fields are simply added
| together to form the result.  This means that any integer portion of `zSig'
| will be added into the exponent.  Since a properly normalized significand
| will have an integer portion equal to 1, the `zExp' input should be 1 less
| than the desired result exponent whenever `zSig' is a complete, normalized
| significand.
*----------------------------------------------------------------------------*/

static inline float64 packFloat64(flag zSign, int zExp, uint64_t zSig)
{

    return make_float64(
        ( ( (uint64_t) zSign )<<63 ) + ( ( (uint64_t) zExp )<<52 ) + zSig);

}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and significand `zSig', and returns the proper double-precision floating-
| point value corresponding to the abstract input.  Ordinarily, the abstract
| value is simply rounded and packed into the double-precision format, with
| the inexact exception raised if the abstract input cannot be represented
| exactly.  However, if the abstract value is too large, the overflow and
| inexact exceptions are raised and an infinity or maximal finite value is
| returned.  If the abstract value is too small, the input value is rounded to
| a subnormal number, and the underflow and inexact exceptions are raised if
| the abstract input cannot be represented exactly as a subnormal double-
| precision floating-point number.
|     The input significand `zSig' has its binary point between bits 62
| and 61, which is 10 bits to the left of the usual location.  This shifted
| significand must be normalized or smaller.  If `zSig' is not normalized,
| `zExp' must be 0; in that case, the result returned is a subnormal number,
| and it must not require rounding.  In the usual case that `zSig' is
| normalized, `zExp' must be 1 less than the ``true'' floating-point exponent.
| The handling of underflow and overflow follows the IEC/IEEE Standard for
| Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static float64 roundAndPackFloat64(flag zSign, int zExp, uint64_t zSig,
                                   float_status *status)
{
    int8_t roundingMode;
    flag roundNearestEven;
    int roundIncrement, roundBits;
    flag isTiny;

    roundingMode = status->float_rounding_mode;
    roundNearestEven = ( roundingMode == float_round_nearest_even );
    switch (roundingMode) {
    case float_round_nearest_even:
    case float_round_ties_away:
        roundIncrement = 0x200;
        break;
    case float_round_to_zero:
        roundIncrement = 0;
        break;
    case float_round_up:
        roundIncrement = zSign ? 0 : 0x3ff;
        break;
    case float_round_down:
        roundIncrement = zSign ? 0x3ff : 0;
        break;
    case float_round_to_odd:
        roundIncrement = (zSig & 0x400) ? 0 : 0x3ff;
        break;
    default:
        abort();
    }
    roundBits = zSig & 0x3FF;
    if ( 0x7FD <= (uint16_t) zExp ) {
        if (    ( 0x7FD < zExp )
             || (    ( zExp == 0x7FD )
                  && ( (int64_t) ( zSig + roundIncrement ) < 0 ) )
           ) {
            bool overflow_to_inf = roundingMode != float_round_to_odd &&
                                   roundIncrement != 0;
            float_raise(float_flag_overflow | float_flag_inexact, status);
            return packFloat64(zSign, 0x7FF, -(!overflow_to_inf));
        }
        if ( zExp < 0 ) {
            if (status->flush_to_zero) {
                float_raise(float_flag_output_denormal, status);
                return packFloat64(zSign, 0, 0);
            }
            isTiny =
                   (status->float_detect_tininess
                    == float_tininess_before_rounding)
                || ( zExp < -1 )
                || ( zSig + roundIncrement < UINT64_C(0x8000000000000000) );
            shift64RightJamming( zSig, - zExp, &zSig );
            zExp = 0;
            roundBits = zSig & 0x3FF;
            if (isTiny && roundBits) {
                float_raise(float_flag_underflow, status);
            }
            if (roundingMode == float_round_to_odd) {
                /*
                 * For round-to-odd case, the roundIncrement depends on
                 * zSig which just changed.
                 */
                roundIncrement = (zSig & 0x400) ? 0 : 0x3ff;
            }
        }
    }
    if (roundBits) {
        status->float_exception_flags |= float_flag_inexact;
    }
    zSig = ( zSig + roundIncrement )>>10;
    zSig &= ~ ( ( ( roundBits ^ 0x200 ) == 0 ) & roundNearestEven );
    if ( zSig == 0 ) zExp = 0;
    return packFloat64( zSign, zExp, zSig );

}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and significand `zSig', and returns the proper double-precision floating-
| point value corresponding to the abstract input.  This routine is just like
| `roundAndPackFloat64' except that `zSig' does not have to be normalized.
| Bit 63 of `zSig' must be zero, and `zExp' must be 1 less than the ``true''
| floating-point exponent.
*----------------------------------------------------------------------------*/

static float64
 normalizeRoundAndPackFloat64(flag zSign, int zExp, uint64_t zSig,
                              float_status *status)
{
    int8_t shiftCount;

    shiftCount = clz64(zSig) - 1;
    return roundAndPackFloat64(zSign, zExp - shiftCount, zSig<<shiftCount,
                               status);

}

/*----------------------------------------------------------------------------
| Normalizes the subnormal extended double-precision floating-point value
| represented by the denormalized significand `aSig'.  The normalized exponent
| and significand are stored at the locations pointed to by `zExpPtr' and
| `zSigPtr', respectively.
*----------------------------------------------------------------------------*/

void normalizeFloatx80Subnormal(uint64_t aSig, int32_t *zExpPtr,
                                uint64_t *zSigPtr)
{
    int8_t shiftCount;

    shiftCount = clz64(aSig);
    *zSigPtr = aSig<<shiftCount;
    *zExpPtr = 1 - shiftCount;
}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and extended significand formed by the concatenation of `zSig0' and `zSig1',
| and returns the proper extended double-precision floating-point value
| corresponding to the abstract input.  Ordinarily, the abstract value is
| rounded and packed into the extended double-precision format, with the
| inexact exception raised if the abstract input cannot be represented
| exactly.  However, if the abstract value is too large, the overflow and
| inexact exceptions are raised and an infinity or maximal finite value is
| returned.  If the abstract value is too small, the input value is rounded to
| a subnormal number, and the underflow and inexact exceptions are raised if
| the abstract input cannot be represented exactly as a subnormal extended
| double-precision floating-point number.
|     If `roundingPrecision' is 32 or 64, the result is rounded to the same
| number of bits as single or double precision, respectively.  Otherwise, the
| result is rounded to the full precision of the extended double-precision
| format.
|     The input significand must be normalized or smaller.  If the input
| significand is not normalized, `zExp' must be 0; in that case, the result
| returned is a subnormal number, and it must not require rounding.  The
| handling of underflow and overflow follows the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 roundAndPackFloatx80(int8_t roundingPrecision, flag zSign,
                              int32_t zExp, uint64_t zSig0, uint64_t zSig1,
                              float_status *status)
{
    int8_t roundingMode;
    flag roundNearestEven, increment, isTiny;
    int64_t roundIncrement, roundMask, roundBits;

    roundingMode = status->float_rounding_mode;
    roundNearestEven = ( roundingMode == float_round_nearest_even );
    if ( roundingPrecision == 80 ) goto precision80;
    if ( roundingPrecision == 64 ) {
        roundIncrement = UINT64_C(0x0000000000000400);
        roundMask = UINT64_C(0x00000000000007FF);
    }
    else if ( roundingPrecision == 32 ) {
        roundIncrement = UINT64_C(0x0000008000000000);
        roundMask = UINT64_C(0x000000FFFFFFFFFF);
    }
    else {
        goto precision80;
    }
    zSig0 |= ( zSig1 != 0 );
    switch (roundingMode) {
    case float_round_nearest_even:
    case float_round_ties_away:
        break;
    case float_round_to_zero:
        roundIncrement = 0;
        break;
    case float_round_up:
        roundIncrement = zSign ? 0 : roundMask;
        break;
    case float_round_down:
        roundIncrement = zSign ? roundMask : 0;
        break;
    default:
        abort();
    }
    roundBits = zSig0 & roundMask;
    if ( 0x7FFD <= (uint32_t) ( zExp - 1 ) ) {
        if (    ( 0x7FFE < zExp )
             || ( ( zExp == 0x7FFE ) && ( zSig0 + roundIncrement < zSig0 ) )
           ) {
            goto overflow;
        }
        if ( zExp <= 0 ) {
            if (status->flush_to_zero) {
                float_raise(float_flag_output_denormal, status);
                return packFloatx80(zSign, 0, 0);
            }
            isTiny =
                   (status->float_detect_tininess
                    == float_tininess_before_rounding)
                || ( zExp < 0 )
                || ( zSig0 <= zSig0 + roundIncrement );
            shift64RightJamming( zSig0, 1 - zExp, &zSig0 );
            zExp = 0;
            roundBits = zSig0 & roundMask;
            if (isTiny && roundBits) {
                float_raise(float_flag_underflow, status);
            }
            if (roundBits) {
                status->float_exception_flags |= float_flag_inexact;
            }
            zSig0 += roundIncrement;
            if ( (int64_t) zSig0 < 0 ) zExp = 1;
            roundIncrement = roundMask + 1;
            if ( roundNearestEven && ( roundBits<<1 == roundIncrement ) ) {
                roundMask |= roundIncrement;
            }
            zSig0 &= ~ roundMask;
            return packFloatx80( zSign, zExp, zSig0 );
        }
    }
    if (roundBits) {
        status->float_exception_flags |= float_flag_inexact;
    }
    zSig0 += roundIncrement;
    if ( zSig0 < roundIncrement ) {
        ++zExp;
        zSig0 = UINT64_C(0x8000000000000000);
    }
    roundIncrement = roundMask + 1;
    if ( roundNearestEven && ( roundBits<<1 == roundIncrement ) ) {
        roundMask |= roundIncrement;
    }
    zSig0 &= ~ roundMask;
    if ( zSig0 == 0 ) zExp = 0;
    return packFloatx80( zSign, zExp, zSig0 );
 precision80:
    switch (roundingMode) {
    case float_round_nearest_even:
    case float_round_ties_away:
        increment = ((int64_t)zSig1 < 0);
        break;
    case float_round_to_zero:
        increment = 0;
        break;
    case float_round_up:
        increment = !zSign && zSig1;
        break;
    case float_round_down:
        increment = zSign && zSig1;
        break;
    default:
        abort();
    }
    if ( 0x7FFD <= (uint32_t) ( zExp - 1 ) ) {
        if (    ( 0x7FFE < zExp )
             || (    ( zExp == 0x7FFE )
                  && ( zSig0 == UINT64_C(0xFFFFFFFFFFFFFFFF) )
                  && increment
                )
           ) {
            roundMask = 0;
 overflow:
            float_raise(float_flag_overflow | float_flag_inexact, status);
            if (    ( roundingMode == float_round_to_zero )
                 || ( zSign && ( roundingMode == float_round_up ) )
                 || ( ! zSign && ( roundingMode == float_round_down ) )
               ) {
                return packFloatx80( zSign, 0x7FFE, ~ roundMask );
            }
            return packFloatx80(zSign,
                                floatx80_infinity_high,
                                floatx80_infinity_low);
        }
        if ( zExp <= 0 ) {
            isTiny =
                   (status->float_detect_tininess
                    == float_tininess_before_rounding)
                || ( zExp < 0 )
                || ! increment
                || ( zSig0 < UINT64_C(0xFFFFFFFFFFFFFFFF) );
            shift64ExtraRightJamming( zSig0, zSig1, 1 - zExp, &zSig0, &zSig1 );
            zExp = 0;
            if (isTiny && zSig1) {
                float_raise(float_flag_underflow, status);
            }
            if (zSig1) {
                status->float_exception_flags |= float_flag_inexact;
            }
            switch (roundingMode) {
            case float_round_nearest_even:
            case float_round_ties_away:
                increment = ((int64_t)zSig1 < 0);
                break;
            case float_round_to_zero:
                increment = 0;
                break;
            case float_round_up:
                increment = !zSign && zSig1;
                break;
            case float_round_down:
                increment = zSign && zSig1;
                break;
            default:
                abort();
            }
            if ( increment ) {
                ++zSig0;
                zSig0 &=
                    ~ ( ( (uint64_t) ( zSig1<<1 ) == 0 ) & roundNearestEven );
                if ( (int64_t) zSig0 < 0 ) zExp = 1;
            }
            return packFloatx80( zSign, zExp, zSig0 );
        }
    }
    if (zSig1) {
        status->float_exception_flags |= float_flag_inexact;
    }
    if ( increment ) {
        ++zSig0;
        if ( zSig0 == 0 ) {
            ++zExp;
            zSig0 = UINT64_C(0x8000000000000000);
        }
        else {
            zSig0 &= ~ ( ( (uint64_t) ( zSig1<<1 ) == 0 ) & roundNearestEven );
        }
    }
    else {
        if ( zSig0 == 0 ) zExp = 0;
    }
    return packFloatx80( zSign, zExp, zSig0 );

}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent
| `zExp', and significand formed by the concatenation of `zSig0' and `zSig1',
| and returns the proper extended double-precision floating-point value
| corresponding to the abstract input.  This routine is just like
| `roundAndPackFloatx80' except that the input significand does not have to be
| normalized.
*----------------------------------------------------------------------------*/

floatx80 normalizeRoundAndPackFloatx80(int8_t roundingPrecision,
                                       flag zSign, int32_t zExp,
                                       uint64_t zSig0, uint64_t zSig1,
                                       float_status *status)
{
    int8_t shiftCount;

    if ( zSig0 == 0 ) {
        zSig0 = zSig1;
        zSig1 = 0;
        zExp -= 64;
    }
    shiftCount = clz64(zSig0);
    shortShift128Left( zSig0, zSig1, shiftCount, &zSig0, &zSig1 );
    zExp -= shiftCount;
    return roundAndPackFloatx80(roundingPrecision, zSign, zExp,
                                zSig0, zSig1, status);

}

/*----------------------------------------------------------------------------
| Returns the least-significant 64 fraction bits of the quadruple-precision
| floating-point value `a'.
*----------------------------------------------------------------------------*/

static inline uint64_t extractFloat128Frac1( float128 a )
{

    return a.low;

}

/*----------------------------------------------------------------------------
| Returns the most-significant 48 fraction bits of the quadruple-precision
| floating-point value `a'.
*----------------------------------------------------------------------------*/

static inline uint64_t extractFloat128Frac0( float128 a )
{

    return a.high & UINT64_C(0x0000FFFFFFFFFFFF);

}

/*----------------------------------------------------------------------------
| Returns the exponent bits of the quadruple-precision floating-point value
| `a'.
*----------------------------------------------------------------------------*/

static inline int32_t extractFloat128Exp( float128 a )
{

    return ( a.high>>48 ) & 0x7FFF;

}

/*----------------------------------------------------------------------------
| Returns the sign bit of the quadruple-precision floating-point value `a'.
*----------------------------------------------------------------------------*/

static inline flag extractFloat128Sign( float128 a )
{

    return a.high>>63;

}

/*----------------------------------------------------------------------------
| Normalizes the subnormal quadruple-precision floating-point value
| represented by the denormalized significand formed by the concatenation of
| `aSig0' and `aSig1'.  The normalized exponent is stored at the location
| pointed to by `zExpPtr'.  The most significant 49 bits of the normalized
| significand are stored at the location pointed to by `zSig0Ptr', and the
| least significant 64 bits of the normalized significand are stored at the
| location pointed to by `zSig1Ptr'.
*----------------------------------------------------------------------------*/

static void
 normalizeFloat128Subnormal(
     uint64_t aSig0,
     uint64_t aSig1,
     int32_t *zExpPtr,
     uint64_t *zSig0Ptr,
     uint64_t *zSig1Ptr
 )
{
    int8_t shiftCount;

    if ( aSig0 == 0 ) {
        shiftCount = clz64(aSig1) - 15;
        if ( shiftCount < 0 ) {
            *zSig0Ptr = aSig1>>( - shiftCount );
            *zSig1Ptr = aSig1<<( shiftCount & 63 );
        }
        else {
            *zSig0Ptr = aSig1<<shiftCount;
            *zSig1Ptr = 0;
        }
        *zExpPtr = - shiftCount - 63;
    }
    else {
        shiftCount = clz64(aSig0) - 15;
        shortShift128Left( aSig0, aSig1, shiftCount, zSig0Ptr, zSig1Ptr );
        *zExpPtr = 1 - shiftCount;
    }

}

/*----------------------------------------------------------------------------
| Packs the sign `zSign', the exponent `zExp', and the significand formed
| by the concatenation of `zSig0' and `zSig1' into a quadruple-precision
| floating-point value, returning the result.  After being shifted into the
| proper positions, the three fields `zSign', `zExp', and `zSig0' are simply
| added together to form the most significant 32 bits of the result.  This
| means that any integer portion of `zSig0' will be added into the exponent.
| Since a properly normalized significand will have an integer portion equal
| to 1, the `zExp' input should be 1 less than the desired result exponent
| whenever `zSig0' and `zSig1' concatenated form a complete, normalized
| significand.
*----------------------------------------------------------------------------*/

static inline float128
 packFloat128( flag zSign, int32_t zExp, uint64_t zSig0, uint64_t zSig1 )
{
    float128 z;

    z.low = zSig1;
    z.high = ( ( (uint64_t) zSign )<<63 ) + ( ( (uint64_t) zExp )<<48 ) + zSig0;
    return z;

}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and extended significand formed by the concatenation of `zSig0', `zSig1',
| and `zSig2', and returns the proper quadruple-precision floating-point value
| corresponding to the abstract input.  Ordinarily, the abstract value is
| simply rounded and packed into the quadruple-precision format, with the
| inexact exception raised if the abstract input cannot be represented
| exactly.  However, if the abstract value is too large, the overflow and
| inexact exceptions are raised and an infinity or maximal finite value is
| returned.  If the abstract value is too small, the input value is rounded to
| a subnormal number, and the underflow and inexact exceptions are raised if
| the abstract input cannot be represented exactly as a subnormal quadruple-
| precision floating-point number.
|     The input significand must be normalized or smaller.  If the input
| significand is not normalized, `zExp' must be 0; in that case, the result
| returned is a subnormal number, and it must not require rounding.  In the
| usual case that the input significand is normalized, `zExp' must be 1 less
| than the ``true'' floating-point exponent.  The handling of underflow and
| overflow follows the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static float128 roundAndPackFloat128(flag zSign, int32_t zExp,
                                     uint64_t zSig0, uint64_t zSig1,
                                     uint64_t zSig2, float_status *status)
{
    int8_t roundingMode;
    flag roundNearestEven, increment, isTiny;

    roundingMode = status->float_rounding_mode;
    roundNearestEven = ( roundingMode == float_round_nearest_even );
    switch (roundingMode) {
    case float_round_nearest_even:
    case float_round_ties_away:
        increment = ((int64_t)zSig2 < 0);
        break;
    case float_round_to_zero:
        increment = 0;
        break;
    case float_round_up:
        increment = !zSign && zSig2;
        break;
    case float_round_down:
        increment = zSign && zSig2;
        break;
    case float_round_to_odd:
        increment = !(zSig1 & 0x1) && zSig2;
        break;
    default:
        abort();
    }
    if ( 0x7FFD <= (uint32_t) zExp ) {
        if (    ( 0x7FFD < zExp )
             || (    ( zExp == 0x7FFD )
                  && eq128(
                         UINT64_C(0x0001FFFFFFFFFFFF),
                         UINT64_C(0xFFFFFFFFFFFFFFFF),
                         zSig0,
                         zSig1
                     )
                  && increment
                )
           ) {
            float_raise(float_flag_overflow | float_flag_inexact, status);
            if (    ( roundingMode == float_round_to_zero )
                 || ( zSign && ( roundingMode == float_round_up ) )
                 || ( ! zSign && ( roundingMode == float_round_down ) )
                 || (roundingMode == float_round_to_odd)
               ) {
                return
                    packFloat128(
                        zSign,
                        0x7FFE,
                        UINT64_C(0x0000FFFFFFFFFFFF),
                        UINT64_C(0xFFFFFFFFFFFFFFFF)
                    );
            }
            return packFloat128( zSign, 0x7FFF, 0, 0 );
        }
        if ( zExp < 0 ) {
            if (status->flush_to_zero) {
                float_raise(float_flag_output_denormal, status);
                return packFloat128(zSign, 0, 0, 0);
            }
            isTiny =
                   (status->float_detect_tininess
                    == float_tininess_before_rounding)
                || ( zExp < -1 )
                || ! increment
                || lt128(
                       zSig0,
                       zSig1,
                       UINT64_C(0x0001FFFFFFFFFFFF),
                       UINT64_C(0xFFFFFFFFFFFFFFFF)
                   );
            shift128ExtraRightJamming(
                zSig0, zSig1, zSig2, - zExp, &zSig0, &zSig1, &zSig2 );
            zExp = 0;
            if (isTiny && zSig2) {
                float_raise(float_flag_underflow, status);
            }
            switch (roundingMode) {
            case float_round_nearest_even:
            case float_round_ties_away:
                increment = ((int64_t)zSig2 < 0);
                break;
            case float_round_to_zero:
                increment = 0;
                break;
            case float_round_up:
                increment = !zSign && zSig2;
                break;
            case float_round_down:
                increment = zSign && zSig2;
                break;
            case float_round_to_odd:
                increment = !(zSig1 & 0x1) && zSig2;
                break;
            default:
                abort();
            }
        }
    }
    if (zSig2) {
        status->float_exception_flags |= float_flag_inexact;
    }
    if ( increment ) {
        add128( zSig0, zSig1, 0, 1, &zSig0, &zSig1 );
        zSig1 &= ~ ( ( zSig2 + zSig2 == 0 ) & roundNearestEven );
    }
    else {
        if ( ( zSig0 | zSig1 ) == 0 ) zExp = 0;
    }
    return packFloat128( zSign, zExp, zSig0, zSig1 );

}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and significand formed by the concatenation of `zSig0' and `zSig1', and
| returns the proper quadruple-precision floating-point value corresponding
| to the abstract input.  This routine is just like `roundAndPackFloat128'
| except that the input significand has fewer bits and does not have to be
| normalized.  In all cases, `zExp' must be 1 less than the ``true'' floating-
| point exponent.
*----------------------------------------------------------------------------*/

static float128 normalizeRoundAndPackFloat128(flag zSign, int32_t zExp,
                                              uint64_t zSig0, uint64_t zSig1,
                                              float_status *status)
{
    int8_t shiftCount;
    uint64_t zSig2;

    if ( zSig0 == 0 ) {
        zSig0 = zSig1;
        zSig1 = 0;
        zExp -= 64;
    }
    shiftCount = clz64(zSig0) - 15;
    if ( 0 <= shiftCount ) {
        zSig2 = 0;
        shortShift128Left( zSig0, zSig1, shiftCount, &zSig0, &zSig1 );
    }
    else {
        shift128ExtraRightJamming(
            zSig0, zSig1, 0, - shiftCount, &zSig0, &zSig1, &zSig2 );
    }
    zExp -= shiftCount;
    return roundAndPackFloat128(zSign, zExp, zSig0, zSig1, zSig2, status);

}


/*----------------------------------------------------------------------------
| Returns the result of converting the 32-bit two's complement integer `a'
| to the extended double-precision floating-point format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 int32_to_floatx80(int32_t a, float_status *status)
{
    flag zSign;
    uint32_t absA;
    int8_t shiftCount;
    uint64_t zSig;

    if ( a == 0 ) return packFloatx80( 0, 0, 0 );
    zSign = ( a < 0 );
    absA = zSign ? - a : a;
    shiftCount = clz32(absA) + 32;
    zSig = absA;
    return packFloatx80( zSign, 0x403E - shiftCount, zSig<<shiftCount );

}

/*----------------------------------------------------------------------------
| Returns the result of converting the 32-bit two's complement integer `a' to
| the quadruple-precision floating-point format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 int32_to_float128(int32_t a, float_status *status)
{
    flag zSign;
    uint32_t absA;
    int8_t shiftCount;
    uint64_t zSig0;

    if ( a == 0 ) return packFloat128( 0, 0, 0, 0 );
    zSign = ( a < 0 );
    absA = zSign ? - a : a;
    shiftCount = clz32(absA) + 17;
    zSig0 = absA;
    return packFloat128( zSign, 0x402E - shiftCount, zSig0<<shiftCount, 0 );

}

/*----------------------------------------------------------------------------
| Returns the result of converting the 64-bit two's complement integer `a'
| to the extended double-precision floating-point format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 int64_to_floatx80(int64_t a, float_status *status)
{
    flag zSign;
    uint64_t absA;
    int8_t shiftCount;

    if ( a == 0 ) return packFloatx80( 0, 0, 0 );
    zSign = ( a < 0 );
    absA = zSign ? - a : a;
    shiftCount = clz64(absA);
    return packFloatx80( zSign, 0x403E - shiftCount, absA<<shiftCount );

}

/*----------------------------------------------------------------------------
| Returns the result of converting the 64-bit two's complement integer `a' to
| the quadruple-precision floating-point format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 int64_to_float128(int64_t a, float_status *status)
{
    flag zSign;
    uint64_t absA;
    int8_t shiftCount;
    int32_t zExp;
    uint64_t zSig0, zSig1;

    if ( a == 0 ) return packFloat128( 0, 0, 0, 0 );
    zSign = ( a < 0 );
    absA = zSign ? - a : a;
    shiftCount = clz64(absA) + 49;
    zExp = 0x406E - shiftCount;
    if ( 64 <= shiftCount ) {
        zSig1 = 0;
        zSig0 = absA;
        shiftCount -= 64;
    }
    else {
        zSig1 = absA;
        zSig0 = 0;
    }
    shortShift128Left( zSig0, zSig1, shiftCount, &zSig0, &zSig1 );
    return packFloat128( zSign, zExp, zSig0, zSig1 );

}

/*----------------------------------------------------------------------------
| Returns the result of converting the 64-bit unsigned integer `a'
| to the quadruple-precision floating-point format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 uint64_to_float128(uint64_t a, float_status *status)
{
    if (a == 0) {
        return float128_zero;
    }
    return normalizeRoundAndPackFloat128(0, 0x406E, 0, a, status);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the extended double-precision floating-point format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 float32_to_floatx80(float32 a, float_status *status)
{
    flag aSign;
    int aExp;
    uint32_t aSig;

    a = float32_squash_input_denormal(a, status);
    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    if ( aExp == 0xFF ) {
        if (aSig) {
            return commonNaNToFloatx80(float32ToCommonNaN(a, status), status);
        }
        return packFloatx80(aSign,
                            floatx80_infinity_high,
                            floatx80_infinity_low);
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloatx80( aSign, 0, 0 );
        normalizeFloat32Subnormal( aSig, &aExp, &aSig );
    }
    aSig |= 0x00800000;
    return packFloatx80( aSign, aExp + 0x3F80, ( (uint64_t) aSig )<<40 );

}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the double-precision floating-point format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

float128 float32_to_float128(float32 a, float_status *status)
{
    flag aSign;
    int aExp;
    uint32_t aSig;

    a = float32_squash_input_denormal(a, status);
    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    if ( aExp == 0xFF ) {
        if (aSig) {
            return commonNaNToFloat128(float32ToCommonNaN(a, status), status);
        }
        return packFloat128( aSign, 0x7FFF, 0, 0 );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloat128( aSign, 0, 0, 0 );
        normalizeFloat32Subnormal( aSig, &aExp, &aSig );
        --aExp;
    }
    return packFloat128( aSign, aExp + 0x3F80, ( (uint64_t) aSig )<<25, 0 );

}

/*----------------------------------------------------------------------------
| Returns the remainder of the single-precision floating-point value `a'
| with respect to the corresponding value `b'.  The operation is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_rem(float32 a, float32 b, float_status *status)
{
    flag aSign, zSign;
    int aExp, bExp, expDiff;
    uint32_t aSig, bSig;
    uint32_t q;
    uint64_t aSig64, bSig64, q64;
    uint32_t alternateASig;
    int32_t sigMean;
    a = float32_squash_input_denormal(a, status);
    b = float32_squash_input_denormal(b, status);

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    bSig = extractFloat32Frac( b );
    bExp = extractFloat32Exp( b );
    if ( aExp == 0xFF ) {
        if ( aSig || ( ( bExp == 0xFF ) && bSig ) ) {
            return propagateFloat32NaN(a, b, status);
        }
        float_raise(float_flag_invalid, status);
        return float32_default_nan(status);
    }
    if ( bExp == 0xFF ) {
        if (bSig) {
            return propagateFloat32NaN(a, b, status);
        }
        return a;
    }
    if ( bExp == 0 ) {
        if ( bSig == 0 ) {
            float_raise(float_flag_invalid, status);
            return float32_default_nan(status);
        }
        normalizeFloat32Subnormal( bSig, &bExp, &bSig );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return a;
        normalizeFloat32Subnormal( aSig, &aExp, &aSig );
    }
    expDiff = aExp - bExp;
    aSig |= 0x00800000;
    bSig |= 0x00800000;
    if ( expDiff < 32 ) {
        aSig <<= 8;
        bSig <<= 8;
        if ( expDiff < 0 ) {
            if ( expDiff < -1 ) return a;
            aSig >>= 1;
        }
        q = ( bSig <= aSig );
        if ( q ) aSig -= bSig;
        if ( 0 < expDiff ) {
            q = ( ( (uint64_t) aSig )<<32 ) / bSig;
            q >>= 32 - expDiff;
            bSig >>= 2;
            aSig = ( ( aSig>>1 )<<( expDiff - 1 ) ) - bSig * q;
        }
        else {
            aSig >>= 2;
            bSig >>= 2;
        }
    }
    else {
        if ( bSig <= aSig ) aSig -= bSig;
        aSig64 = ( (uint64_t) aSig )<<40;
        bSig64 = ( (uint64_t) bSig )<<40;
        expDiff -= 64;
        while ( 0 < expDiff ) {
            q64 = estimateDiv128To64( aSig64, 0, bSig64 );
            q64 = ( 2 < q64 ) ? q64 - 2 : 0;
            aSig64 = - ( ( bSig * q64 )<<38 );
            expDiff -= 62;
        }
        expDiff += 64;
        q64 = estimateDiv128To64( aSig64, 0, bSig64 );
        q64 = ( 2 < q64 ) ? q64 - 2 : 0;
        q = q64>>( 64 - expDiff );
        bSig <<= 6;
        aSig = ( ( aSig64>>33 )<<( expDiff - 1 ) ) - bSig * q;
    }
    do {
        alternateASig = aSig;
        ++q;
        aSig -= bSig;
    } while ( 0 <= (int32_t) aSig );
    sigMean = aSig + alternateASig;
    if ( ( sigMean < 0 ) || ( ( sigMean == 0 ) && ( q & 1 ) ) ) {
        aSig = alternateASig;
    }
    zSign = ( (int32_t) aSig < 0 );
    if ( zSign ) aSig = - aSig;
    return normalizeRoundAndPackFloat32(aSign ^ zSign, bExp, aSig, status);
}



/*----------------------------------------------------------------------------
| Returns the binary exponential of the single-precision floating-point value
| `a'. The operation is performed according to the IEC/IEEE Standard for
| Binary Floating-Point Arithmetic.
|
| Uses the following identities:
|
| 1. -------------------------------------------------------------------------
|      x    x*ln(2)
|     2  = e
|
| 2. -------------------------------------------------------------------------
|                      2     3     4     5           n
|      x        x     x     x     x     x           x
|     e  = 1 + --- + --- + --- + --- + --- + ... + --- + ...
|               1!    2!    3!    4!    5!          n!
*----------------------------------------------------------------------------*/

static const float64 float32_exp2_coefficients[15] =
{
    const_float64( 0x3ff0000000000000ll ), /*  1 */
    const_float64( 0x3fe0000000000000ll ), /*  2 */
    const_float64( 0x3fc5555555555555ll ), /*  3 */
    const_float64( 0x3fa5555555555555ll ), /*  4 */
    const_float64( 0x3f81111111111111ll ), /*  5 */
    const_float64( 0x3f56c16c16c16c17ll ), /*  6 */
    const_float64( 0x3f2a01a01a01a01all ), /*  7 */
    const_float64( 0x3efa01a01a01a01all ), /*  8 */
    const_float64( 0x3ec71de3a556c734ll ), /*  9 */
    const_float64( 0x3e927e4fb7789f5cll ), /* 10 */
    const_float64( 0x3e5ae64567f544e4ll ), /* 11 */
    const_float64( 0x3e21eed8eff8d898ll ), /* 12 */
    const_float64( 0x3de6124613a86d09ll ), /* 13 */
    const_float64( 0x3da93974a8c07c9dll ), /* 14 */
    const_float64( 0x3d6ae7f3e733b81fll ), /* 15 */
};

float32 float32_exp2(float32 a, float_status *status)
{
    flag aSign;
    int aExp;
    uint32_t aSig;
    float64 r, x, xn;
    int i;
    a = float32_squash_input_denormal(a, status);

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );

    if ( aExp == 0xFF) {
        if (aSig) {
            return propagateFloat32NaN(a, float32_zero, status);
        }
        return (aSign) ? float32_zero : a;
    }
    if (aExp == 0) {
        if (aSig == 0) return float32_one;
    }

    float_raise(float_flag_inexact, status);

    /* ******************************* */
    /* using float64 for approximation */
    /* ******************************* */
    x = float32_to_float64(a, status);
    x = float64_mul(x, float64_ln2, status);

    xn = x;
    r = float64_one;
    for (i = 0 ; i < 15 ; i++) {
        float64 f;

        f = float64_mul(xn, float32_exp2_coefficients[i], status);
        r = float64_add(r, f, status);

        xn = float64_mul(xn, x, status);
    }

    return float64_to_float32(r, status);
}

/*----------------------------------------------------------------------------
| Returns the binary log of the single-precision floating-point value `a'.
| The operation is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/
float32 float32_log2(float32 a, float_status *status)
{
    flag aSign, zSign;
    int aExp;
    uint32_t aSig, zSig, i;

    a = float32_squash_input_denormal(a, status);
    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );

    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloat32( 1, 0xFF, 0 );
        normalizeFloat32Subnormal( aSig, &aExp, &aSig );
    }
    if ( aSign ) {
        float_raise(float_flag_invalid, status);
        return float32_default_nan(status);
    }
    if ( aExp == 0xFF ) {
        if (aSig) {
            return propagateFloat32NaN(a, float32_zero, status);
        }
        return a;
    }

    aExp -= 0x7F;
    aSig |= 0x00800000;
    zSign = aExp < 0;
    zSig = aExp << 23;

    for (i = 1 << 22; i > 0; i >>= 1) {
        aSig = ( (uint64_t)aSig * aSig ) >> 23;
        if ( aSig & 0x01000000 ) {
            aSig >>= 1;
            zSig |= i;
        }
    }

    if ( zSign )
        zSig = -zSig;

    return normalizeRoundAndPackFloat32(zSign, 0x85, zSig, status);
}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is equal to
| the corresponding value `b', and 0 otherwise.  The invalid exception is
| raised if either operand is a NaN.  Otherwise, the comparison is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float32_eq(float32 a, float32 b, float_status *status)
{
    uint32_t av, bv;
    a = float32_squash_input_denormal(a, status);
    b = float32_squash_input_denormal(b, status);

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    av = float32_val(a);
    bv = float32_val(b);
    return ( av == bv ) || ( (uint32_t) ( ( av | bv )<<1 ) == 0 );
}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is less than
| or equal to the corresponding value `b', and 0 otherwise.  The invalid
| exception is raised if either operand is a NaN.  The comparison is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float32_le(float32 a, float32 b, float_status *status)
{
    flag aSign, bSign;
    uint32_t av, bv;
    a = float32_squash_input_denormal(a, status);
    b = float32_squash_input_denormal(b, status);

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    aSign = extractFloat32Sign( a );
    bSign = extractFloat32Sign( b );
    av = float32_val(a);
    bv = float32_val(b);
    if ( aSign != bSign ) return aSign || ( (uint32_t) ( ( av | bv )<<1 ) == 0 );
    return ( av == bv ) || ( aSign ^ ( av < bv ) );

}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is less than
| the corresponding value `b', and 0 otherwise.  The invalid exception is
| raised if either operand is a NaN.  The comparison is performed according
| to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float32_lt(float32 a, float32 b, float_status *status)
{
    flag aSign, bSign;
    uint32_t av, bv;
    a = float32_squash_input_denormal(a, status);
    b = float32_squash_input_denormal(b, status);

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    aSign = extractFloat32Sign( a );
    bSign = extractFloat32Sign( b );
    av = float32_val(a);
    bv = float32_val(b);
    if ( aSign != bSign ) return aSign && ( (uint32_t) ( ( av | bv )<<1 ) != 0 );
    return ( av != bv ) && ( aSign ^ ( av < bv ) );

}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point values `a' and `b' cannot
| be compared, and 0 otherwise.  The invalid exception is raised if either
| operand is a NaN.  The comparison is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float32_unordered(float32 a, float32 b, float_status *status)
{
    a = float32_squash_input_denormal(a, status);
    b = float32_squash_input_denormal(b, status);

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        float_raise(float_flag_invalid, status);
        return 1;
    }
    return 0;
}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is equal to
| the corresponding value `b', and 0 otherwise.  Quiet NaNs do not cause an
| exception.  The comparison is performed according to the IEC/IEEE Standard
| for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float32_eq_quiet(float32 a, float32 b, float_status *status)
{
    a = float32_squash_input_denormal(a, status);
    b = float32_squash_input_denormal(b, status);

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        if (float32_is_signaling_nan(a, status)
         || float32_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 0;
    }
    return ( float32_val(a) == float32_val(b) ) ||
            ( (uint32_t) ( ( float32_val(a) | float32_val(b) )<<1 ) == 0 );
}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is less than or
| equal to the corresponding value `b', and 0 otherwise.  Quiet NaNs do not
| cause an exception.  Otherwise, the comparison is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float32_le_quiet(float32 a, float32 b, float_status *status)
{
    flag aSign, bSign;
    uint32_t av, bv;
    a = float32_squash_input_denormal(a, status);
    b = float32_squash_input_denormal(b, status);

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        if (float32_is_signaling_nan(a, status)
         || float32_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 0;
    }
    aSign = extractFloat32Sign( a );
    bSign = extractFloat32Sign( b );
    av = float32_val(a);
    bv = float32_val(b);
    if ( aSign != bSign ) return aSign || ( (uint32_t) ( ( av | bv )<<1 ) == 0 );
    return ( av == bv ) || ( aSign ^ ( av < bv ) );

}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is less than
| the corresponding value `b', and 0 otherwise.  Quiet NaNs do not cause an
| exception.  Otherwise, the comparison is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float32_lt_quiet(float32 a, float32 b, float_status *status)
{
    flag aSign, bSign;
    uint32_t av, bv;
    a = float32_squash_input_denormal(a, status);
    b = float32_squash_input_denormal(b, status);

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        if (float32_is_signaling_nan(a, status)
         || float32_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 0;
    }
    aSign = extractFloat32Sign( a );
    bSign = extractFloat32Sign( b );
    av = float32_val(a);
    bv = float32_val(b);
    if ( aSign != bSign ) return aSign && ( (uint32_t) ( ( av | bv )<<1 ) != 0 );
    return ( av != bv ) && ( aSign ^ ( av < bv ) );

}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point values `a' and `b' cannot
| be compared, and 0 otherwise.  Quiet NaNs do not cause an exception.  The
| comparison is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float32_unordered_quiet(float32 a, float32 b, float_status *status)
{
    a = float32_squash_input_denormal(a, status);
    b = float32_squash_input_denormal(b, status);

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        if (float32_is_signaling_nan(a, status)
         || float32_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 1;
    }
    return 0;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-precision floating-point value
| `a' to the extended double-precision floating-point format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 float64_to_floatx80(float64 a, float_status *status)
{
    flag aSign;
    int aExp;
    uint64_t aSig;

    a = float64_squash_input_denormal(a, status);
    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    if ( aExp == 0x7FF ) {
        if (aSig) {
            return commonNaNToFloatx80(float64ToCommonNaN(a, status), status);
        }
        return packFloatx80(aSign,
                            floatx80_infinity_high,
                            floatx80_infinity_low);
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloatx80( aSign, 0, 0 );
        normalizeFloat64Subnormal( aSig, &aExp, &aSig );
    }
    return
        packFloatx80(
            aSign, aExp + 0x3C00, (aSig | UINT64_C(0x0010000000000000)) << 11);

}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-precision floating-point value
| `a' to the quadruple-precision floating-point format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

float128 float64_to_float128(float64 a, float_status *status)
{
    flag aSign;
    int aExp;
    uint64_t aSig, zSig0, zSig1;

    a = float64_squash_input_denormal(a, status);
    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    if ( aExp == 0x7FF ) {
        if (aSig) {
            return commonNaNToFloat128(float64ToCommonNaN(a, status), status);
        }
        return packFloat128( aSign, 0x7FFF, 0, 0 );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloat128( aSign, 0, 0, 0 );
        normalizeFloat64Subnormal( aSig, &aExp, &aSig );
        --aExp;
    }
    shift128Right( aSig, 0, 4, &zSig0, &zSig1 );
    return packFloat128( aSign, aExp + 0x3C00, zSig0, zSig1 );

}


/*----------------------------------------------------------------------------
| Returns the remainder of the double-precision floating-point value `a'
| with respect to the corresponding value `b'.  The operation is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 float64_rem(float64 a, float64 b, float_status *status)
{
    flag aSign, zSign;
    int aExp, bExp, expDiff;
    uint64_t aSig, bSig;
    uint64_t q, alternateASig;
    int64_t sigMean;

    a = float64_squash_input_denormal(a, status);
    b = float64_squash_input_denormal(b, status);
    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    bSig = extractFloat64Frac( b );
    bExp = extractFloat64Exp( b );
    if ( aExp == 0x7FF ) {
        if ( aSig || ( ( bExp == 0x7FF ) && bSig ) ) {
            return propagateFloat64NaN(a, b, status);
        }
        float_raise(float_flag_invalid, status);
        return float64_default_nan(status);
    }
    if ( bExp == 0x7FF ) {
        if (bSig) {
            return propagateFloat64NaN(a, b, status);
        }
        return a;
    }
    if ( bExp == 0 ) {
        if ( bSig == 0 ) {
            float_raise(float_flag_invalid, status);
            return float64_default_nan(status);
        }
        normalizeFloat64Subnormal( bSig, &bExp, &bSig );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return a;
        normalizeFloat64Subnormal( aSig, &aExp, &aSig );
    }
    expDiff = aExp - bExp;
    aSig = (aSig | UINT64_C(0x0010000000000000)) << 11;
    bSig = (bSig | UINT64_C(0x0010000000000000)) << 11;
    if ( expDiff < 0 ) {
        if ( expDiff < -1 ) return a;
        aSig >>= 1;
    }
    q = ( bSig <= aSig );
    if ( q ) aSig -= bSig;
    expDiff -= 64;
    while ( 0 < expDiff ) {
        q = estimateDiv128To64( aSig, 0, bSig );
        q = ( 2 < q ) ? q - 2 : 0;
        aSig = - ( ( bSig>>2 ) * q );
        expDiff -= 62;
    }
    expDiff += 64;
    if ( 0 < expDiff ) {
        q = estimateDiv128To64( aSig, 0, bSig );
        q = ( 2 < q ) ? q - 2 : 0;
        q >>= 64 - expDiff;
        bSig >>= 2;
        aSig = ( ( aSig>>1 )<<( expDiff - 1 ) ) - bSig * q;
    }
    else {
        aSig >>= 2;
        bSig >>= 2;
    }
    do {
        alternateASig = aSig;
        ++q;
        aSig -= bSig;
    } while ( 0 <= (int64_t) aSig );
    sigMean = aSig + alternateASig;
    if ( ( sigMean < 0 ) || ( ( sigMean == 0 ) && ( q & 1 ) ) ) {
        aSig = alternateASig;
    }
    zSign = ( (int64_t) aSig < 0 );
    if ( zSign ) aSig = - aSig;
    return normalizeRoundAndPackFloat64(aSign ^ zSign, bExp, aSig, status);

}

/*----------------------------------------------------------------------------
| Returns the binary log of the double-precision floating-point value `a'.
| The operation is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/
float64 float64_log2(float64 a, float_status *status)
{
    flag aSign, zSign;
    int aExp;
    uint64_t aSig, aSig0, aSig1, zSig, i;
    a = float64_squash_input_denormal(a, status);

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );

    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloat64( 1, 0x7FF, 0 );
        normalizeFloat64Subnormal( aSig, &aExp, &aSig );
    }
    if ( aSign ) {
        float_raise(float_flag_invalid, status);
        return float64_default_nan(status);
    }
    if ( aExp == 0x7FF ) {
        if (aSig) {
            return propagateFloat64NaN(a, float64_zero, status);
        }
        return a;
    }

    aExp -= 0x3FF;
    aSig |= UINT64_C(0x0010000000000000);
    zSign = aExp < 0;
    zSig = (uint64_t)aExp << 52;
    for (i = 1LL << 51; i > 0; i >>= 1) {
        mul64To128( aSig, aSig, &aSig0, &aSig1 );
        aSig = ( aSig0 << 12 ) | ( aSig1 >> 52 );
        if ( aSig & UINT64_C(0x0020000000000000) ) {
            aSig >>= 1;
            zSig |= i;
        }
    }

    if ( zSign )
        zSig = -zSig;
    return normalizeRoundAndPackFloat64(zSign, 0x408, zSig, status);
}

/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point value `a' is equal to the
| corresponding value `b', and 0 otherwise.  The invalid exception is raised
| if either operand is a NaN.  Otherwise, the comparison is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float64_eq(float64 a, float64 b, float_status *status)
{
    uint64_t av, bv;
    a = float64_squash_input_denormal(a, status);
    b = float64_squash_input_denormal(b, status);

    if (    ( ( extractFloat64Exp( a ) == 0x7FF ) && extractFloat64Frac( a ) )
         || ( ( extractFloat64Exp( b ) == 0x7FF ) && extractFloat64Frac( b ) )
       ) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    av = float64_val(a);
    bv = float64_val(b);
    return ( av == bv ) || ( (uint64_t) ( ( av | bv )<<1 ) == 0 );

}

/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point value `a' is less than or
| equal to the corresponding value `b', and 0 otherwise.  The invalid
| exception is raised if either operand is a NaN.  The comparison is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float64_le(float64 a, float64 b, float_status *status)
{
    flag aSign, bSign;
    uint64_t av, bv;
    a = float64_squash_input_denormal(a, status);
    b = float64_squash_input_denormal(b, status);

    if (    ( ( extractFloat64Exp( a ) == 0x7FF ) && extractFloat64Frac( a ) )
         || ( ( extractFloat64Exp( b ) == 0x7FF ) && extractFloat64Frac( b ) )
       ) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    aSign = extractFloat64Sign( a );
    bSign = extractFloat64Sign( b );
    av = float64_val(a);
    bv = float64_val(b);
    if ( aSign != bSign ) return aSign || ( (uint64_t) ( ( av | bv )<<1 ) == 0 );
    return ( av == bv ) || ( aSign ^ ( av < bv ) );

}

/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point value `a' is less than
| the corresponding value `b', and 0 otherwise.  The invalid exception is
| raised if either operand is a NaN.  The comparison is performed according
| to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float64_lt(float64 a, float64 b, float_status *status)
{
    flag aSign, bSign;
    uint64_t av, bv;

    a = float64_squash_input_denormal(a, status);
    b = float64_squash_input_denormal(b, status);
    if (    ( ( extractFloat64Exp( a ) == 0x7FF ) && extractFloat64Frac( a ) )
         || ( ( extractFloat64Exp( b ) == 0x7FF ) && extractFloat64Frac( b ) )
       ) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    aSign = extractFloat64Sign( a );
    bSign = extractFloat64Sign( b );
    av = float64_val(a);
    bv = float64_val(b);
    if ( aSign != bSign ) return aSign && ( (uint64_t) ( ( av | bv )<<1 ) != 0 );
    return ( av != bv ) && ( aSign ^ ( av < bv ) );

}

/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point values `a' and `b' cannot
| be compared, and 0 otherwise.  The invalid exception is raised if either
| operand is a NaN.  The comparison is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float64_unordered(float64 a, float64 b, float_status *status)
{
    a = float64_squash_input_denormal(a, status);
    b = float64_squash_input_denormal(b, status);

    if (    ( ( extractFloat64Exp( a ) == 0x7FF ) && extractFloat64Frac( a ) )
         || ( ( extractFloat64Exp( b ) == 0x7FF ) && extractFloat64Frac( b ) )
       ) {
        float_raise(float_flag_invalid, status);
        return 1;
    }
    return 0;
}

/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point value `a' is equal to the
| corresponding value `b', and 0 otherwise.  Quiet NaNs do not cause an
| exception.The comparison is performed according to the IEC/IEEE Standard
| for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float64_eq_quiet(float64 a, float64 b, float_status *status)
{
    uint64_t av, bv;
    a = float64_squash_input_denormal(a, status);
    b = float64_squash_input_denormal(b, status);

    if (    ( ( extractFloat64Exp( a ) == 0x7FF ) && extractFloat64Frac( a ) )
         || ( ( extractFloat64Exp( b ) == 0x7FF ) && extractFloat64Frac( b ) )
       ) {
        if (float64_is_signaling_nan(a, status)
         || float64_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 0;
    }
    av = float64_val(a);
    bv = float64_val(b);
    return ( av == bv ) || ( (uint64_t) ( ( av | bv )<<1 ) == 0 );

}

/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point value `a' is less than or
| equal to the corresponding value `b', and 0 otherwise.  Quiet NaNs do not
| cause an exception.  Otherwise, the comparison is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float64_le_quiet(float64 a, float64 b, float_status *status)
{
    flag aSign, bSign;
    uint64_t av, bv;
    a = float64_squash_input_denormal(a, status);
    b = float64_squash_input_denormal(b, status);

    if (    ( ( extractFloat64Exp( a ) == 0x7FF ) && extractFloat64Frac( a ) )
         || ( ( extractFloat64Exp( b ) == 0x7FF ) && extractFloat64Frac( b ) )
       ) {
        if (float64_is_signaling_nan(a, status)
         || float64_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 0;
    }
    aSign = extractFloat64Sign( a );
    bSign = extractFloat64Sign( b );
    av = float64_val(a);
    bv = float64_val(b);
    if ( aSign != bSign ) return aSign || ( (uint64_t) ( ( av | bv )<<1 ) == 0 );
    return ( av == bv ) || ( aSign ^ ( av < bv ) );

}

/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point value `a' is less than
| the corresponding value `b', and 0 otherwise.  Quiet NaNs do not cause an
| exception.  Otherwise, the comparison is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float64_lt_quiet(float64 a, float64 b, float_status *status)
{
    flag aSign, bSign;
    uint64_t av, bv;
    a = float64_squash_input_denormal(a, status);
    b = float64_squash_input_denormal(b, status);

    if (    ( ( extractFloat64Exp( a ) == 0x7FF ) && extractFloat64Frac( a ) )
         || ( ( extractFloat64Exp( b ) == 0x7FF ) && extractFloat64Frac( b ) )
       ) {
        if (float64_is_signaling_nan(a, status)
         || float64_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 0;
    }
    aSign = extractFloat64Sign( a );
    bSign = extractFloat64Sign( b );
    av = float64_val(a);
    bv = float64_val(b);
    if ( aSign != bSign ) return aSign && ( (uint64_t) ( ( av | bv )<<1 ) != 0 );
    return ( av != bv ) && ( aSign ^ ( av < bv ) );

}

/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point values `a' and `b' cannot
| be compared, and 0 otherwise.  Quiet NaNs do not cause an exception.  The
| comparison is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float64_unordered_quiet(float64 a, float64 b, float_status *status)
{
    a = float64_squash_input_denormal(a, status);
    b = float64_squash_input_denormal(b, status);

    if (    ( ( extractFloat64Exp( a ) == 0x7FF ) && extractFloat64Frac( a ) )
         || ( ( extractFloat64Exp( b ) == 0x7FF ) && extractFloat64Frac( b ) )
       ) {
        if (float64_is_signaling_nan(a, status)
         || float64_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 1;
    }
    return 0;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point value `a' to the 32-bit two's complement integer format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic---which means in particular that the conversion
| is rounded according to the current rounding mode.  If `a' is a NaN, the
| largest positive integer is returned.  Otherwise, if the conversion
| overflows, the largest integer with the same sign as `a' is returned.
*----------------------------------------------------------------------------*/

int32_t floatx80_to_int32(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp, shiftCount;
    uint64_t aSig;

    if (floatx80_invalid_encoding(a)) {
        float_raise(float_flag_invalid, status);
        return 1 << 31;
    }
    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    if ( ( aExp == 0x7FFF ) && (uint64_t) ( aSig<<1 ) ) aSign = 0;
    shiftCount = 0x4037 - aExp;
    if ( shiftCount <= 0 ) shiftCount = 1;
    shift64RightJamming( aSig, shiftCount, &aSig );
    return roundAndPackInt32(aSign, aSig, status);

}

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point value `a' to the 32-bit two's complement integer format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic, except that the conversion is always rounded
| toward zero.  If `a' is a NaN, the largest positive integer is returned.
| Otherwise, if the conversion overflows, the largest integer with the same
| sign as `a' is returned.
*----------------------------------------------------------------------------*/

int32_t floatx80_to_int32_round_to_zero(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp, shiftCount;
    uint64_t aSig, savedASig;
    int32_t z;

    if (floatx80_invalid_encoding(a)) {
        float_raise(float_flag_invalid, status);
        return 1 << 31;
    }
    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    if ( 0x401E < aExp ) {
        if ( ( aExp == 0x7FFF ) && (uint64_t) ( aSig<<1 ) ) aSign = 0;
        goto invalid;
    }
    else if ( aExp < 0x3FFF ) {
        if (aExp || aSig) {
            status->float_exception_flags |= float_flag_inexact;
        }
        return 0;
    }
    shiftCount = 0x403E - aExp;
    savedASig = aSig;
    aSig >>= shiftCount;
    z = aSig;
    if ( aSign ) z = - z;
    if ( ( z < 0 ) ^ aSign ) {
 invalid:
        float_raise(float_flag_invalid, status);
        return aSign ? (int32_t) 0x80000000 : 0x7FFFFFFF;
    }
    if ( ( aSig<<shiftCount ) != savedASig ) {
        status->float_exception_flags |= float_flag_inexact;
    }
    return z;

}

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point value `a' to the 64-bit two's complement integer format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic---which means in particular that the conversion
| is rounded according to the current rounding mode.  If `a' is a NaN,
| the largest positive integer is returned.  Otherwise, if the conversion
| overflows, the largest integer with the same sign as `a' is returned.
*----------------------------------------------------------------------------*/

int64_t floatx80_to_int64(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp, shiftCount;
    uint64_t aSig, aSigExtra;

    if (floatx80_invalid_encoding(a)) {
        float_raise(float_flag_invalid, status);
        return 1ULL << 63;
    }
    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    shiftCount = 0x403E - aExp;
    if ( shiftCount <= 0 ) {
        if ( shiftCount ) {
            float_raise(float_flag_invalid, status);
            if (!aSign || floatx80_is_any_nan(a)) {
                return INT64_MAX;
            }
            return INT64_MIN;
        }
        aSigExtra = 0;
    }
    else {
        shift64ExtraRightJamming( aSig, 0, shiftCount, &aSig, &aSigExtra );
    }
    return roundAndPackInt64(aSign, aSig, aSigExtra, status);

}

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point value `a' to the 64-bit two's complement integer format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic, except that the conversion is always rounded
| toward zero.  If `a' is a NaN, the largest positive integer is returned.
| Otherwise, if the conversion overflows, the largest integer with the same
| sign as `a' is returned.
*----------------------------------------------------------------------------*/

int64_t floatx80_to_int64_round_to_zero(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp, shiftCount;
    uint64_t aSig;
    int64_t z;

    if (floatx80_invalid_encoding(a)) {
        float_raise(float_flag_invalid, status);
        return 1ULL << 63;
    }
    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    shiftCount = aExp - 0x403E;
    if ( 0 <= shiftCount ) {
        aSig &= UINT64_C(0x7FFFFFFFFFFFFFFF);
        if ( ( a.high != 0xC03E ) || aSig ) {
            float_raise(float_flag_invalid, status);
            if ( ! aSign || ( ( aExp == 0x7FFF ) && aSig ) ) {
                return INT64_MAX;
            }
        }
        return INT64_MIN;
    }
    else if ( aExp < 0x3FFF ) {
        if (aExp | aSig) {
            status->float_exception_flags |= float_flag_inexact;
        }
        return 0;
    }
    z = aSig>>( - shiftCount );
    if ( (uint64_t) ( aSig<<( shiftCount & 63 ) ) ) {
        status->float_exception_flags |= float_flag_inexact;
    }
    if ( aSign ) z = - z;
    return z;

}

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point value `a' to the single-precision floating-point format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 floatx80_to_float32(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    if (floatx80_invalid_encoding(a)) {
        float_raise(float_flag_invalid, status);
        return float32_default_nan(status);
    }
    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    if ( aExp == 0x7FFF ) {
        if ( (uint64_t) ( aSig<<1 ) ) {
            return commonNaNToFloat32(floatx80ToCommonNaN(a, status), status);
        }
        return packFloat32( aSign, 0xFF, 0 );
    }
    shift64RightJamming( aSig, 33, &aSig );
    if ( aExp || aSig ) aExp -= 0x3F81;
    return roundAndPackFloat32(aSign, aExp, aSig, status);

}

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point value `a' to the double-precision floating-point format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 floatx80_to_float64(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig, zSig;

    if (floatx80_invalid_encoding(a)) {
        float_raise(float_flag_invalid, status);
        return float64_default_nan(status);
    }
    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    if ( aExp == 0x7FFF ) {
        if ( (uint64_t) ( aSig<<1 ) ) {
            return commonNaNToFloat64(floatx80ToCommonNaN(a, status), status);
        }
        return packFloat64( aSign, 0x7FF, 0 );
    }
    shift64RightJamming( aSig, 1, &zSig );
    if ( aExp || aSig ) aExp -= 0x3C01;
    return roundAndPackFloat64(aSign, aExp, zSig, status);

}

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point value `a' to the quadruple-precision floating-point format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 floatx80_to_float128(floatx80 a, float_status *status)
{
    flag aSign;
    int aExp;
    uint64_t aSig, zSig0, zSig1;

    if (floatx80_invalid_encoding(a)) {
        float_raise(float_flag_invalid, status);
        return float128_default_nan(status);
    }
    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    if ( ( aExp == 0x7FFF ) && (uint64_t) ( aSig<<1 ) ) {
        return commonNaNToFloat128(floatx80ToCommonNaN(a, status), status);
    }
    shift128Right( aSig<<1, 0, 16, &zSig0, &zSig1 );
    return packFloat128( aSign, aExp, zSig0, zSig1 );

}

/*----------------------------------------------------------------------------
| Rounds the extended double-precision floating-point value `a'
| to the precision provided by floatx80_rounding_precision and returns the
| result as an extended double-precision floating-point value.
| The operation is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 floatx80_round(floatx80 a, float_status *status)
{
    return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                extractFloatx80Sign(a),
                                extractFloatx80Exp(a),
                                extractFloatx80Frac(a), 0, status);
}

/*----------------------------------------------------------------------------
| Rounds the extended double-precision floating-point value `a' to an integer,
| and returns the result as an extended quadruple-precision floating-point
| value.  The operation is performed according to the IEC/IEEE Standard for
| Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 floatx80_round_to_int(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t lastBitMask, roundBitsMask;
    floatx80 z;

    if (floatx80_invalid_encoding(a)) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }
    aExp = extractFloatx80Exp( a );
    if ( 0x403E <= aExp ) {
        if ( ( aExp == 0x7FFF ) && (uint64_t) ( extractFloatx80Frac( a )<<1 ) ) {
            return propagateFloatx80NaN(a, a, status);
        }
        return a;
    }
    if ( aExp < 0x3FFF ) {
        if (    ( aExp == 0 )
             && ( (uint64_t) ( extractFloatx80Frac( a )<<1 ) == 0 ) ) {
            return a;
        }
        status->float_exception_flags |= float_flag_inexact;
        aSign = extractFloatx80Sign( a );
        switch (status->float_rounding_mode) {
         case float_round_nearest_even:
            if ( ( aExp == 0x3FFE ) && (uint64_t) ( extractFloatx80Frac( a )<<1 )
               ) {
                return
                    packFloatx80( aSign, 0x3FFF, UINT64_C(0x8000000000000000));
            }
            break;
        case float_round_ties_away:
            if (aExp == 0x3FFE) {
                return packFloatx80(aSign, 0x3FFF, UINT64_C(0x8000000000000000));
            }
            break;
         case float_round_down:
            return
                  aSign ?
                      packFloatx80( 1, 0x3FFF, UINT64_C(0x8000000000000000))
                : packFloatx80( 0, 0, 0 );
         case float_round_up:
            return
                  aSign ? packFloatx80( 1, 0, 0 )
                : packFloatx80( 0, 0x3FFF, UINT64_C(0x8000000000000000));
        }
        return packFloatx80( aSign, 0, 0 );
    }
    lastBitMask = 1;
    lastBitMask <<= 0x403E - aExp;
    roundBitsMask = lastBitMask - 1;
    z = a;
    switch (status->float_rounding_mode) {
    case float_round_nearest_even:
        z.low += lastBitMask>>1;
        if ((z.low & roundBitsMask) == 0) {
            z.low &= ~lastBitMask;
        }
        break;
    case float_round_ties_away:
        z.low += lastBitMask >> 1;
        break;
    case float_round_to_zero:
        break;
    case float_round_up:
        if (!extractFloatx80Sign(z)) {
            z.low += roundBitsMask;
        }
        break;
    case float_round_down:
        if (extractFloatx80Sign(z)) {
            z.low += roundBitsMask;
        }
        break;
    default:
        abort();
    }
    z.low &= ~ roundBitsMask;
    if ( z.low == 0 ) {
        ++z.high;
        z.low = UINT64_C(0x8000000000000000);
    }
    if (z.low != a.low) {
        status->float_exception_flags |= float_flag_inexact;
    }
    return z;

}

/*----------------------------------------------------------------------------
| Returns the result of adding the absolute values of the extended double-
| precision floating-point values `a' and `b'.  If `zSign' is 1, the sum is
| negated before being returned.  `zSign' is ignored if the result is a NaN.
| The addition is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static floatx80 addFloatx80Sigs(floatx80 a, floatx80 b, flag zSign,
                                float_status *status)
{
    int32_t aExp, bExp, zExp;
    uint64_t aSig, bSig, zSig0, zSig1;
    int32_t expDiff;

    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    bSig = extractFloatx80Frac( b );
    bExp = extractFloatx80Exp( b );
    expDiff = aExp - bExp;
    if ( 0 < expDiff ) {
        if ( aExp == 0x7FFF ) {
            if ((uint64_t)(aSig << 1)) {
                return propagateFloatx80NaN(a, b, status);
            }
            return a;
        }
        if ( bExp == 0 ) --expDiff;
        shift64ExtraRightJamming( bSig, 0, expDiff, &bSig, &zSig1 );
        zExp = aExp;
    }
    else if ( expDiff < 0 ) {
        if ( bExp == 0x7FFF ) {
            if ((uint64_t)(bSig << 1)) {
                return propagateFloatx80NaN(a, b, status);
            }
            return packFloatx80(zSign,
                                floatx80_infinity_high,
                                floatx80_infinity_low);
        }
        if ( aExp == 0 ) ++expDiff;
        shift64ExtraRightJamming( aSig, 0, - expDiff, &aSig, &zSig1 );
        zExp = bExp;
    }
    else {
        if ( aExp == 0x7FFF ) {
            if ( (uint64_t) ( ( aSig | bSig )<<1 ) ) {
                return propagateFloatx80NaN(a, b, status);
            }
            return a;
        }
        zSig1 = 0;
        zSig0 = aSig + bSig;
        if ( aExp == 0 ) {
            if (zSig0 == 0) {
                return packFloatx80(zSign, 0, 0);
            }
            normalizeFloatx80Subnormal( zSig0, &zExp, &zSig0 );
            goto roundAndPack;
        }
        zExp = aExp;
        goto shiftRight1;
    }
    zSig0 = aSig + bSig;
    if ( (int64_t) zSig0 < 0 ) goto roundAndPack;
 shiftRight1:
    shift64ExtraRightJamming( zSig0, zSig1, 1, &zSig0, &zSig1 );
    zSig0 |= UINT64_C(0x8000000000000000);
    ++zExp;
 roundAndPack:
    return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                zSign, zExp, zSig0, zSig1, status);
}

/*----------------------------------------------------------------------------
| Returns the result of subtracting the absolute values of the extended
| double-precision floating-point values `a' and `b'.  If `zSign' is 1, the
| difference is negated before being returned.  `zSign' is ignored if the
| result is a NaN.  The subtraction is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static floatx80 subFloatx80Sigs(floatx80 a, floatx80 b, flag zSign,
                                float_status *status)
{
    int32_t aExp, bExp, zExp;
    uint64_t aSig, bSig, zSig0, zSig1;
    int32_t expDiff;

    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    bSig = extractFloatx80Frac( b );
    bExp = extractFloatx80Exp( b );
    expDiff = aExp - bExp;
    if ( 0 < expDiff ) goto aExpBigger;
    if ( expDiff < 0 ) goto bExpBigger;
    if ( aExp == 0x7FFF ) {
        if ( (uint64_t) ( ( aSig | bSig )<<1 ) ) {
            return propagateFloatx80NaN(a, b, status);
        }
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }
    if ( aExp == 0 ) {
        aExp = 1;
        bExp = 1;
    }
    zSig1 = 0;
    if ( bSig < aSig ) goto aBigger;
    if ( aSig < bSig ) goto bBigger;
    return packFloatx80(status->float_rounding_mode == float_round_down, 0, 0);
 bExpBigger:
    if ( bExp == 0x7FFF ) {
        if ((uint64_t)(bSig << 1)) {
            return propagateFloatx80NaN(a, b, status);
        }
        return packFloatx80(zSign ^ 1, floatx80_infinity_high,
                            floatx80_infinity_low);
    }
    if ( aExp == 0 ) ++expDiff;
    shift128RightJamming( aSig, 0, - expDiff, &aSig, &zSig1 );
 bBigger:
    sub128( bSig, 0, aSig, zSig1, &zSig0, &zSig1 );
    zExp = bExp;
    zSign ^= 1;
    goto normalizeRoundAndPack;
 aExpBigger:
    if ( aExp == 0x7FFF ) {
        if ((uint64_t)(aSig << 1)) {
            return propagateFloatx80NaN(a, b, status);
        }
        return a;
    }
    if ( bExp == 0 ) --expDiff;
    shift128RightJamming( bSig, 0, expDiff, &bSig, &zSig1 );
 aBigger:
    sub128( aSig, 0, bSig, zSig1, &zSig0, &zSig1 );
    zExp = aExp;
 normalizeRoundAndPack:
    return normalizeRoundAndPackFloatx80(status->floatx80_rounding_precision,
                                         zSign, zExp, zSig0, zSig1, status);
}

/*----------------------------------------------------------------------------
| Returns the result of adding the extended double-precision floating-point
| values `a' and `b'.  The operation is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 floatx80_add(floatx80 a, floatx80 b, float_status *status)
{
    flag aSign, bSign;

    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }
    aSign = extractFloatx80Sign( a );
    bSign = extractFloatx80Sign( b );
    if ( aSign == bSign ) {
        return addFloatx80Sigs(a, b, aSign, status);
    }
    else {
        return subFloatx80Sigs(a, b, aSign, status);
    }

}

/*----------------------------------------------------------------------------
| Returns the result of subtracting the extended double-precision floating-
| point values `a' and `b'.  The operation is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 floatx80_sub(floatx80 a, floatx80 b, float_status *status)
{
    flag aSign, bSign;

    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }
    aSign = extractFloatx80Sign( a );
    bSign = extractFloatx80Sign( b );
    if ( aSign == bSign ) {
        return subFloatx80Sigs(a, b, aSign, status);
    }
    else {
        return addFloatx80Sigs(a, b, aSign, status);
    }

}

/*----------------------------------------------------------------------------
| Returns the result of multiplying the extended double-precision floating-
| point values `a' and `b'.  The operation is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 floatx80_mul(floatx80 a, floatx80 b, float_status *status)
{
    flag aSign, bSign, zSign;
    int32_t aExp, bExp, zExp;
    uint64_t aSig, bSig, zSig0, zSig1;

    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }
    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    bSig = extractFloatx80Frac( b );
    bExp = extractFloatx80Exp( b );
    bSign = extractFloatx80Sign( b );
    zSign = aSign ^ bSign;
    if ( aExp == 0x7FFF ) {
        if (    (uint64_t) ( aSig<<1 )
             || ( ( bExp == 0x7FFF ) && (uint64_t) ( bSig<<1 ) ) ) {
            return propagateFloatx80NaN(a, b, status);
        }
        if ( ( bExp | bSig ) == 0 ) goto invalid;
        return packFloatx80(zSign, floatx80_infinity_high,
                                   floatx80_infinity_low);
    }
    if ( bExp == 0x7FFF ) {
        if ((uint64_t)(bSig << 1)) {
            return propagateFloatx80NaN(a, b, status);
        }
        if ( ( aExp | aSig ) == 0 ) {
 invalid:
            float_raise(float_flag_invalid, status);
            return floatx80_default_nan(status);
        }
        return packFloatx80(zSign, floatx80_infinity_high,
                                   floatx80_infinity_low);
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloatx80( zSign, 0, 0 );
        normalizeFloatx80Subnormal( aSig, &aExp, &aSig );
    }
    if ( bExp == 0 ) {
        if ( bSig == 0 ) return packFloatx80( zSign, 0, 0 );
        normalizeFloatx80Subnormal( bSig, &bExp, &bSig );
    }
    zExp = aExp + bExp - 0x3FFE;
    mul64To128( aSig, bSig, &zSig0, &zSig1 );
    if ( 0 < (int64_t) zSig0 ) {
        shortShift128Left( zSig0, zSig1, 1, &zSig0, &zSig1 );
        --zExp;
    }
    return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                zSign, zExp, zSig0, zSig1, status);
}

/*----------------------------------------------------------------------------
| Returns the result of dividing the extended double-precision floating-point
| value `a' by the corresponding value `b'.  The operation is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 floatx80_div(floatx80 a, floatx80 b, float_status *status)
{
    flag aSign, bSign, zSign;
    int32_t aExp, bExp, zExp;
    uint64_t aSig, bSig, zSig0, zSig1;
    uint64_t rem0, rem1, rem2, term0, term1, term2;

    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }
    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    bSig = extractFloatx80Frac( b );
    bExp = extractFloatx80Exp( b );
    bSign = extractFloatx80Sign( b );
    zSign = aSign ^ bSign;
    if ( aExp == 0x7FFF ) {
        if ((uint64_t)(aSig << 1)) {
            return propagateFloatx80NaN(a, b, status);
        }
        if ( bExp == 0x7FFF ) {
            if ((uint64_t)(bSig << 1)) {
                return propagateFloatx80NaN(a, b, status);
            }
            goto invalid;
        }
        return packFloatx80(zSign, floatx80_infinity_high,
                                   floatx80_infinity_low);
    }
    if ( bExp == 0x7FFF ) {
        if ((uint64_t)(bSig << 1)) {
            return propagateFloatx80NaN(a, b, status);
        }
        return packFloatx80( zSign, 0, 0 );
    }
    if ( bExp == 0 ) {
        if ( bSig == 0 ) {
            if ( ( aExp | aSig ) == 0 ) {
 invalid:
                float_raise(float_flag_invalid, status);
                return floatx80_default_nan(status);
            }
            float_raise(float_flag_divbyzero, status);
            return packFloatx80(zSign, floatx80_infinity_high,
                                       floatx80_infinity_low);
        }
        normalizeFloatx80Subnormal( bSig, &bExp, &bSig );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloatx80( zSign, 0, 0 );
        normalizeFloatx80Subnormal( aSig, &aExp, &aSig );
    }
    zExp = aExp - bExp + 0x3FFE;
    rem1 = 0;
    if ( bSig <= aSig ) {
        shift128Right( aSig, 0, 1, &aSig, &rem1 );
        ++zExp;
    }
    zSig0 = estimateDiv128To64( aSig, rem1, bSig );
    mul64To128( bSig, zSig0, &term0, &term1 );
    sub128( aSig, rem1, term0, term1, &rem0, &rem1 );
    while ( (int64_t) rem0 < 0 ) {
        --zSig0;
        add128( rem0, rem1, 0, bSig, &rem0, &rem1 );
    }
    zSig1 = estimateDiv128To64( rem1, 0, bSig );
    if ( (uint64_t) ( zSig1<<1 ) <= 8 ) {
        mul64To128( bSig, zSig1, &term1, &term2 );
        sub128( rem1, 0, term1, term2, &rem1, &rem2 );
        while ( (int64_t) rem1 < 0 ) {
            --zSig1;
            add128( rem1, rem2, 0, bSig, &rem1, &rem2 );
        }
        zSig1 |= ( ( rem1 | rem2 ) != 0 );
    }
    return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                zSign, zExp, zSig0, zSig1, status);
}

/*----------------------------------------------------------------------------
| Returns the remainder of the extended double-precision floating-point value
| `a' with respect to the corresponding value `b'.  The operation is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 floatx80_rem(floatx80 a, floatx80 b, float_status *status)
{
    flag aSign, zSign;
    int32_t aExp, bExp, expDiff;
    uint64_t aSig0, aSig1, bSig;
    uint64_t q, term0, term1, alternateASig0, alternateASig1;

    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }
    aSig0 = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    bSig = extractFloatx80Frac( b );
    bExp = extractFloatx80Exp( b );
    if ( aExp == 0x7FFF ) {
        if (    (uint64_t) ( aSig0<<1 )
             || ( ( bExp == 0x7FFF ) && (uint64_t) ( bSig<<1 ) ) ) {
            return propagateFloatx80NaN(a, b, status);
        }
        goto invalid;
    }
    if ( bExp == 0x7FFF ) {
        if ((uint64_t)(bSig << 1)) {
            return propagateFloatx80NaN(a, b, status);
        }
        return a;
    }
    if ( bExp == 0 ) {
        if ( bSig == 0 ) {
 invalid:
            float_raise(float_flag_invalid, status);
            return floatx80_default_nan(status);
        }
        normalizeFloatx80Subnormal( bSig, &bExp, &bSig );
    }
    if ( aExp == 0 ) {
        if ( (uint64_t) ( aSig0<<1 ) == 0 ) return a;
        normalizeFloatx80Subnormal( aSig0, &aExp, &aSig0 );
    }
    bSig |= UINT64_C(0x8000000000000000);
    zSign = aSign;
    expDiff = aExp - bExp;
    aSig1 = 0;
    if ( expDiff < 0 ) {
        if ( expDiff < -1 ) return a;
        shift128Right( aSig0, 0, 1, &aSig0, &aSig1 );
        expDiff = 0;
    }
    q = ( bSig <= aSig0 );
    if ( q ) aSig0 -= bSig;
    expDiff -= 64;
    while ( 0 < expDiff ) {
        q = estimateDiv128To64( aSig0, aSig1, bSig );
        q = ( 2 < q ) ? q - 2 : 0;
        mul64To128( bSig, q, &term0, &term1 );
        sub128( aSig0, aSig1, term0, term1, &aSig0, &aSig1 );
        shortShift128Left( aSig0, aSig1, 62, &aSig0, &aSig1 );
        expDiff -= 62;
    }
    expDiff += 64;
    if ( 0 < expDiff ) {
        q = estimateDiv128To64( aSig0, aSig1, bSig );
        q = ( 2 < q ) ? q - 2 : 0;
        q >>= 64 - expDiff;
        mul64To128( bSig, q<<( 64 - expDiff ), &term0, &term1 );
        sub128( aSig0, aSig1, term0, term1, &aSig0, &aSig1 );
        shortShift128Left( 0, bSig, 64 - expDiff, &term0, &term1 );
        while ( le128( term0, term1, aSig0, aSig1 ) ) {
            ++q;
            sub128( aSig0, aSig1, term0, term1, &aSig0, &aSig1 );
        }
    }
    else {
        term1 = 0;
        term0 = bSig;
    }
    sub128( term0, term1, aSig0, aSig1, &alternateASig0, &alternateASig1 );
    if (    lt128( alternateASig0, alternateASig1, aSig0, aSig1 )
         || (    eq128( alternateASig0, alternateASig1, aSig0, aSig1 )
              && ( q & 1 ) )
       ) {
        aSig0 = alternateASig0;
        aSig1 = alternateASig1;
        zSign = ! zSign;
    }
    return
        normalizeRoundAndPackFloatx80(
            80, zSign, bExp + expDiff, aSig0, aSig1, status);

}

/*----------------------------------------------------------------------------
| Returns the square root of the extended double-precision floating-point
| value `a'.  The operation is performed according to the IEC/IEEE Standard
| for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 floatx80_sqrt(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp, zExp;
    uint64_t aSig0, aSig1, zSig0, zSig1, doubleZSig0;
    uint64_t rem0, rem1, rem2, rem3, term0, term1, term2, term3;

    if (floatx80_invalid_encoding(a)) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }
    aSig0 = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    if ( aExp == 0x7FFF ) {
        if ((uint64_t)(aSig0 << 1)) {
            return propagateFloatx80NaN(a, a, status);
        }
        if ( ! aSign ) return a;
        goto invalid;
    }
    if ( aSign ) {
        if ( ( aExp | aSig0 ) == 0 ) return a;
 invalid:
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }
    if ( aExp == 0 ) {
        if ( aSig0 == 0 ) return packFloatx80( 0, 0, 0 );
        normalizeFloatx80Subnormal( aSig0, &aExp, &aSig0 );
    }
    zExp = ( ( aExp - 0x3FFF )>>1 ) + 0x3FFF;
    zSig0 = estimateSqrt32( aExp, aSig0>>32 );
    shift128Right( aSig0, 0, 2 + ( aExp & 1 ), &aSig0, &aSig1 );
    zSig0 = estimateDiv128To64( aSig0, aSig1, zSig0<<32 ) + ( zSig0<<30 );
    doubleZSig0 = zSig0<<1;
    mul64To128( zSig0, zSig0, &term0, &term1 );
    sub128( aSig0, aSig1, term0, term1, &rem0, &rem1 );
    while ( (int64_t) rem0 < 0 ) {
        --zSig0;
        doubleZSig0 -= 2;
        add128( rem0, rem1, zSig0>>63, doubleZSig0 | 1, &rem0, &rem1 );
    }
    zSig1 = estimateDiv128To64( rem1, 0, doubleZSig0 );
    if ( ( zSig1 & UINT64_C(0x3FFFFFFFFFFFFFFF) ) <= 5 ) {
        if ( zSig1 == 0 ) zSig1 = 1;
        mul64To128( doubleZSig0, zSig1, &term1, &term2 );
        sub128( rem1, 0, term1, term2, &rem1, &rem2 );
        mul64To128( zSig1, zSig1, &term2, &term3 );
        sub192( rem1, rem2, 0, 0, term2, term3, &rem1, &rem2, &rem3 );
        while ( (int64_t) rem1 < 0 ) {
            --zSig1;
            shortShift128Left( 0, zSig1, 1, &term2, &term3 );
            term3 |= 1;
            term2 |= doubleZSig0;
            add192( rem1, rem2, rem3, 0, term2, term3, &rem1, &rem2, &rem3 );
        }
        zSig1 |= ( ( rem1 | rem2 | rem3 ) != 0 );
    }
    shortShift128Left( 0, zSig1, 1, &zSig0, &zSig1 );
    zSig0 |= doubleZSig0;
    return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                0, zExp, zSig0, zSig1, status);
}

/*----------------------------------------------------------------------------
| Returns 1 if the extended double-precision floating-point value `a' is equal
| to the corresponding value `b', and 0 otherwise.  The invalid exception is
| raised if either operand is a NaN.  Otherwise, the comparison is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int floatx80_eq(floatx80 a, floatx80 b, float_status *status)
{

    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)
        || (extractFloatx80Exp(a) == 0x7FFF
            && (uint64_t) (extractFloatx80Frac(a) << 1))
        || (extractFloatx80Exp(b) == 0x7FFF
            && (uint64_t) (extractFloatx80Frac(b) << 1))
       ) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    return
           ( a.low == b.low )
        && (    ( a.high == b.high )
             || (    ( a.low == 0 )
                  && ( (uint16_t) ( ( a.high | b.high )<<1 ) == 0 ) )
           );

}

/*----------------------------------------------------------------------------
| Returns 1 if the extended double-precision floating-point value `a' is
| less than or equal to the corresponding value `b', and 0 otherwise.  The
| invalid exception is raised if either operand is a NaN.  The comparison is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

int floatx80_le(floatx80 a, floatx80 b, float_status *status)
{
    flag aSign, bSign;

    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)
        || (extractFloatx80Exp(a) == 0x7FFF
            && (uint64_t) (extractFloatx80Frac(a) << 1))
        || (extractFloatx80Exp(b) == 0x7FFF
            && (uint64_t) (extractFloatx80Frac(b) << 1))
       ) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    aSign = extractFloatx80Sign( a );
    bSign = extractFloatx80Sign( b );
    if ( aSign != bSign ) {
        return
               aSign
            || (    ( ( (uint16_t) ( ( a.high | b.high )<<1 ) ) | a.low | b.low )
                 == 0 );
    }
    return
          aSign ? le128( b.high, b.low, a.high, a.low )
        : le128( a.high, a.low, b.high, b.low );

}

/*----------------------------------------------------------------------------
| Returns 1 if the extended double-precision floating-point value `a' is
| less than the corresponding value `b', and 0 otherwise.  The invalid
| exception is raised if either operand is a NaN.  The comparison is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int floatx80_lt(floatx80 a, floatx80 b, float_status *status)
{
    flag aSign, bSign;

    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)
        || (extractFloatx80Exp(a) == 0x7FFF
            && (uint64_t) (extractFloatx80Frac(a) << 1))
        || (extractFloatx80Exp(b) == 0x7FFF
            && (uint64_t) (extractFloatx80Frac(b) << 1))
       ) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    aSign = extractFloatx80Sign( a );
    bSign = extractFloatx80Sign( b );
    if ( aSign != bSign ) {
        return
               aSign
            && (    ( ( (uint16_t) ( ( a.high | b.high )<<1 ) ) | a.low | b.low )
                 != 0 );
    }
    return
          aSign ? lt128( b.high, b.low, a.high, a.low )
        : lt128( a.high, a.low, b.high, b.low );

}

/*----------------------------------------------------------------------------
| Returns 1 if the extended double-precision floating-point values `a' and `b'
| cannot be compared, and 0 otherwise.  The invalid exception is raised if
| either operand is a NaN.   The comparison is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/
int floatx80_unordered(floatx80 a, floatx80 b, float_status *status)
{
    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)
        || (extractFloatx80Exp(a) == 0x7FFF
            && (uint64_t) (extractFloatx80Frac(a) << 1))
        || (extractFloatx80Exp(b) == 0x7FFF
            && (uint64_t) (extractFloatx80Frac(b) << 1))
       ) {
        float_raise(float_flag_invalid, status);
        return 1;
    }
    return 0;
}

/*----------------------------------------------------------------------------
| Returns 1 if the extended double-precision floating-point value `a' is
| equal to the corresponding value `b', and 0 otherwise.  Quiet NaNs do not
| cause an exception.  The comparison is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int floatx80_eq_quiet(floatx80 a, floatx80 b, float_status *status)
{

    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    if (    (    ( extractFloatx80Exp( a ) == 0x7FFF )
              && (uint64_t) ( extractFloatx80Frac( a )<<1 ) )
         || (    ( extractFloatx80Exp( b ) == 0x7FFF )
              && (uint64_t) ( extractFloatx80Frac( b )<<1 ) )
       ) {
        if (floatx80_is_signaling_nan(a, status)
         || floatx80_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 0;
    }
    return
           ( a.low == b.low )
        && (    ( a.high == b.high )
             || (    ( a.low == 0 )
                  && ( (uint16_t) ( ( a.high | b.high )<<1 ) == 0 ) )
           );

}

/*----------------------------------------------------------------------------
| Returns 1 if the extended double-precision floating-point value `a' is less
| than or equal to the corresponding value `b', and 0 otherwise.  Quiet NaNs
| do not cause an exception.  Otherwise, the comparison is performed according
| to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int floatx80_le_quiet(floatx80 a, floatx80 b, float_status *status)
{
    flag aSign, bSign;

    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    if (    (    ( extractFloatx80Exp( a ) == 0x7FFF )
              && (uint64_t) ( extractFloatx80Frac( a )<<1 ) )
         || (    ( extractFloatx80Exp( b ) == 0x7FFF )
              && (uint64_t) ( extractFloatx80Frac( b )<<1 ) )
       ) {
        if (floatx80_is_signaling_nan(a, status)
         || floatx80_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 0;
    }
    aSign = extractFloatx80Sign( a );
    bSign = extractFloatx80Sign( b );
    if ( aSign != bSign ) {
        return
               aSign
            || (    ( ( (uint16_t) ( ( a.high | b.high )<<1 ) ) | a.low | b.low )
                 == 0 );
    }
    return
          aSign ? le128( b.high, b.low, a.high, a.low )
        : le128( a.high, a.low, b.high, b.low );

}

/*----------------------------------------------------------------------------
| Returns 1 if the extended double-precision floating-point value `a' is less
| than the corresponding value `b', and 0 otherwise.  Quiet NaNs do not cause
| an exception.  Otherwise, the comparison is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int floatx80_lt_quiet(floatx80 a, floatx80 b, float_status *status)
{
    flag aSign, bSign;

    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    if (    (    ( extractFloatx80Exp( a ) == 0x7FFF )
              && (uint64_t) ( extractFloatx80Frac( a )<<1 ) )
         || (    ( extractFloatx80Exp( b ) == 0x7FFF )
              && (uint64_t) ( extractFloatx80Frac( b )<<1 ) )
       ) {
        if (floatx80_is_signaling_nan(a, status)
         || floatx80_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 0;
    }
    aSign = extractFloatx80Sign( a );
    bSign = extractFloatx80Sign( b );
    if ( aSign != bSign ) {
        return
               aSign
            && (    ( ( (uint16_t) ( ( a.high | b.high )<<1 ) ) | a.low | b.low )
                 != 0 );
    }
    return
          aSign ? lt128( b.high, b.low, a.high, a.low )
        : lt128( a.high, a.low, b.high, b.low );

}

/*----------------------------------------------------------------------------
| Returns 1 if the extended double-precision floating-point values `a' and `b'
| cannot be compared, and 0 otherwise.  Quiet NaNs do not cause an exception.
| The comparison is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/
int floatx80_unordered_quiet(floatx80 a, floatx80 b, float_status *status)
{
    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)) {
        float_raise(float_flag_invalid, status);
        return 1;
    }
    if (    (    ( extractFloatx80Exp( a ) == 0x7FFF )
              && (uint64_t) ( extractFloatx80Frac( a )<<1 ) )
         || (    ( extractFloatx80Exp( b ) == 0x7FFF )
              && (uint64_t) ( extractFloatx80Frac( b )<<1 ) )
       ) {
        if (floatx80_is_signaling_nan(a, status)
         || floatx80_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 1;
    }
    return 0;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the quadruple-precision floating-point
| value `a' to the 32-bit two's complement integer format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic---which means in particular that the conversion is rounded
| according to the current rounding mode.  If `a' is a NaN, the largest
| positive integer is returned.  Otherwise, if the conversion overflows, the
| largest integer with the same sign as `a' is returned.
*----------------------------------------------------------------------------*/

int32_t float128_to_int32(float128 a, float_status *status)
{
    flag aSign;
    int32_t aExp, shiftCount;
    uint64_t aSig0, aSig1;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    aSign = extractFloat128Sign( a );
    if ( ( aExp == 0x7FFF ) && ( aSig0 | aSig1 ) ) aSign = 0;
    if ( aExp ) aSig0 |= UINT64_C(0x0001000000000000);
    aSig0 |= ( aSig1 != 0 );
    shiftCount = 0x4028 - aExp;
    if ( 0 < shiftCount ) shift64RightJamming( aSig0, shiftCount, &aSig0 );
    return roundAndPackInt32(aSign, aSig0, status);

}

/*----------------------------------------------------------------------------
| Returns the result of converting the quadruple-precision floating-point
| value `a' to the 32-bit two's complement integer format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic, except that the conversion is always rounded toward zero.  If
| `a' is a NaN, the largest positive integer is returned.  Otherwise, if the
| conversion overflows, the largest integer with the same sign as `a' is
| returned.
*----------------------------------------------------------------------------*/

int32_t float128_to_int32_round_to_zero(float128 a, float_status *status)
{
    flag aSign;
    int32_t aExp, shiftCount;
    uint64_t aSig0, aSig1, savedASig;
    int32_t z;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    aSign = extractFloat128Sign( a );
    aSig0 |= ( aSig1 != 0 );
    if ( 0x401E < aExp ) {
        if ( ( aExp == 0x7FFF ) && aSig0 ) aSign = 0;
        goto invalid;
    }
    else if ( aExp < 0x3FFF ) {
        if (aExp || aSig0) {
            status->float_exception_flags |= float_flag_inexact;
        }
        return 0;
    }
    aSig0 |= UINT64_C(0x0001000000000000);
    shiftCount = 0x402F - aExp;
    savedASig = aSig0;
    aSig0 >>= shiftCount;
    z = aSig0;
    if ( aSign ) z = - z;
    if ( ( z < 0 ) ^ aSign ) {
 invalid:
        float_raise(float_flag_invalid, status);
        return aSign ? INT32_MIN : INT32_MAX;
    }
    if ( ( aSig0<<shiftCount ) != savedASig ) {
        status->float_exception_flags |= float_flag_inexact;
    }
    return z;

}

/*----------------------------------------------------------------------------
| Returns the result of converting the quadruple-precision floating-point
| value `a' to the 64-bit two's complement integer format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic---which means in particular that the conversion is rounded
| according to the current rounding mode.  If `a' is a NaN, the largest
| positive integer is returned.  Otherwise, if the conversion overflows, the
| largest integer with the same sign as `a' is returned.
*----------------------------------------------------------------------------*/

int64_t float128_to_int64(float128 a, float_status *status)
{
    flag aSign;
    int32_t aExp, shiftCount;
    uint64_t aSig0, aSig1;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    aSign = extractFloat128Sign( a );
    if ( aExp ) aSig0 |= UINT64_C(0x0001000000000000);
    shiftCount = 0x402F - aExp;
    if ( shiftCount <= 0 ) {
        if ( 0x403E < aExp ) {
            float_raise(float_flag_invalid, status);
            if (    ! aSign
                 || (    ( aExp == 0x7FFF )
                      && ( aSig1 || ( aSig0 != UINT64_C(0x0001000000000000) ) )
                    )
               ) {
                return INT64_MAX;
            }
            return INT64_MIN;
        }
        shortShift128Left( aSig0, aSig1, - shiftCount, &aSig0, &aSig1 );
    }
    else {
        shift64ExtraRightJamming( aSig0, aSig1, shiftCount, &aSig0, &aSig1 );
    }
    return roundAndPackInt64(aSign, aSig0, aSig1, status);

}

/*----------------------------------------------------------------------------
| Returns the result of converting the quadruple-precision floating-point
| value `a' to the 64-bit two's complement integer format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic, except that the conversion is always rounded toward zero.
| If `a' is a NaN, the largest positive integer is returned.  Otherwise, if
| the conversion overflows, the largest integer with the same sign as `a' is
| returned.
*----------------------------------------------------------------------------*/

int64_t float128_to_int64_round_to_zero(float128 a, float_status *status)
{
    flag aSign;
    int32_t aExp, shiftCount;
    uint64_t aSig0, aSig1;
    int64_t z;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    aSign = extractFloat128Sign( a );
    if ( aExp ) aSig0 |= UINT64_C(0x0001000000000000);
    shiftCount = aExp - 0x402F;
    if ( 0 < shiftCount ) {
        if ( 0x403E <= aExp ) {
            aSig0 &= UINT64_C(0x0000FFFFFFFFFFFF);
            if (    ( a.high == UINT64_C(0xC03E000000000000) )
                 && ( aSig1 < UINT64_C(0x0002000000000000) ) ) {
                if (aSig1) {
                    status->float_exception_flags |= float_flag_inexact;
                }
            }
            else {
                float_raise(float_flag_invalid, status);
                if ( ! aSign || ( ( aExp == 0x7FFF ) && ( aSig0 | aSig1 ) ) ) {
                    return INT64_MAX;
                }
            }
            return INT64_MIN;
        }
        z = ( aSig0<<shiftCount ) | ( aSig1>>( ( - shiftCount ) & 63 ) );
        if ( (uint64_t) ( aSig1<<shiftCount ) ) {
            status->float_exception_flags |= float_flag_inexact;
        }
    }
    else {
        if ( aExp < 0x3FFF ) {
            if ( aExp | aSig0 | aSig1 ) {
                status->float_exception_flags |= float_flag_inexact;
            }
            return 0;
        }
        z = aSig0>>( - shiftCount );
        if (    aSig1
             || ( shiftCount && (uint64_t) ( aSig0<<( shiftCount & 63 ) ) ) ) {
            status->float_exception_flags |= float_flag_inexact;
        }
    }
    if ( aSign ) z = - z;
    return z;

}

/*----------------------------------------------------------------------------
| Returns the result of converting the quadruple-precision floating-point value
| `a' to the 64-bit unsigned integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic---which means in particular that the conversion is rounded
| according to the current rounding mode.  If `a' is a NaN, the largest
| positive integer is returned.  If the conversion overflows, the
| largest unsigned integer is returned.  If 'a' is negative, the value is
| rounded and zero is returned; negative values that do not round to zero
| will raise the inexact exception.
*----------------------------------------------------------------------------*/

uint64_t float128_to_uint64(float128 a, float_status *status)
{
    flag aSign;
    int aExp;
    int shiftCount;
    uint64_t aSig0, aSig1;

    aSig0 = extractFloat128Frac0(a);
    aSig1 = extractFloat128Frac1(a);
    aExp = extractFloat128Exp(a);
    aSign = extractFloat128Sign(a);
    if (aSign && (aExp > 0x3FFE)) {
        float_raise(float_flag_invalid, status);
        if (float128_is_any_nan(a)) {
            return UINT64_MAX;
        } else {
            return 0;
        }
    }
    if (aExp) {
        aSig0 |= UINT64_C(0x0001000000000000);
    }
    shiftCount = 0x402F - aExp;
    if (shiftCount <= 0) {
        if (0x403E < aExp) {
            float_raise(float_flag_invalid, status);
            return UINT64_MAX;
        }
        shortShift128Left(aSig0, aSig1, -shiftCount, &aSig0, &aSig1);
    } else {
        shift64ExtraRightJamming(aSig0, aSig1, shiftCount, &aSig0, &aSig1);
    }
    return roundAndPackUint64(aSign, aSig0, aSig1, status);
}

uint64_t float128_to_uint64_round_to_zero(float128 a, float_status *status)
{
    uint64_t v;
    signed char current_rounding_mode = status->float_rounding_mode;

    set_float_rounding_mode(float_round_to_zero, status);
    v = float128_to_uint64(a, status);
    set_float_rounding_mode(current_rounding_mode, status);

    return v;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the quadruple-precision floating-point
| value `a' to the 32-bit unsigned integer format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic except that the conversion is always rounded toward zero.
| If `a' is a NaN, the largest positive integer is returned.  Otherwise,
| if the conversion overflows, the largest unsigned integer is returned.
| If 'a' is negative, the value is rounded and zero is returned; negative
| values that do not round to zero will raise the inexact exception.
*----------------------------------------------------------------------------*/

uint32_t float128_to_uint32_round_to_zero(float128 a, float_status *status)
{
    uint64_t v;
    uint32_t res;
    int old_exc_flags = get_float_exception_flags(status);

    v = float128_to_uint64_round_to_zero(a, status);
    if (v > 0xffffffff) {
        res = 0xffffffff;
    } else {
        return v;
    }
    set_float_exception_flags(old_exc_flags, status);
    float_raise(float_flag_invalid, status);
    return res;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the quadruple-precision floating-point value
| `a' to the 32-bit unsigned integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic---which means in particular that the conversion is rounded
| according to the current rounding mode.  If `a' is a NaN, the largest
| positive integer is returned.  If the conversion overflows, the
| largest unsigned integer is returned.  If 'a' is negative, the value is
| rounded and zero is returned; negative values that do not round to zero
| will raise the inexact exception.
*----------------------------------------------------------------------------*/

uint32_t float128_to_uint32(float128 a, float_status *status)
{
    uint64_t v;
    uint32_t res;
    int old_exc_flags = get_float_exception_flags(status);

    v = float128_to_uint64(a, status);
    if (v > 0xffffffff) {
        res = 0xffffffff;
    } else {
        return v;
    }
    set_float_exception_flags(old_exc_flags, status);
    float_raise(float_flag_invalid, status);
    return res;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the quadruple-precision floating-point
| value `a' to the single-precision floating-point format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

float32 float128_to_float32(float128 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig0, aSig1;
    uint32_t zSig;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    aSign = extractFloat128Sign( a );
    if ( aExp == 0x7FFF ) {
        if ( aSig0 | aSig1 ) {
            return commonNaNToFloat32(float128ToCommonNaN(a, status), status);
        }
        return packFloat32( aSign, 0xFF, 0 );
    }
    aSig0 |= ( aSig1 != 0 );
    shift64RightJamming( aSig0, 18, &aSig0 );
    zSig = aSig0;
    if ( aExp || zSig ) {
        zSig |= 0x40000000;
        aExp -= 0x3F81;
    }
    return roundAndPackFloat32(aSign, aExp, zSig, status);

}

/*----------------------------------------------------------------------------
| Returns the result of converting the quadruple-precision floating-point
| value `a' to the double-precision floating-point format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

float64 float128_to_float64(float128 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig0, aSig1;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    aSign = extractFloat128Sign( a );
    if ( aExp == 0x7FFF ) {
        if ( aSig0 | aSig1 ) {
            return commonNaNToFloat64(float128ToCommonNaN(a, status), status);
        }
        return packFloat64( aSign, 0x7FF, 0 );
    }
    shortShift128Left( aSig0, aSig1, 14, &aSig0, &aSig1 );
    aSig0 |= ( aSig1 != 0 );
    if ( aExp || aSig0 ) {
        aSig0 |= UINT64_C(0x4000000000000000);
        aExp -= 0x3C01;
    }
    return roundAndPackFloat64(aSign, aExp, aSig0, status);

}

/*----------------------------------------------------------------------------
| Returns the result of converting the quadruple-precision floating-point
| value `a' to the extended double-precision floating-point format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 float128_to_floatx80(float128 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig0, aSig1;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    aSign = extractFloat128Sign( a );
    if ( aExp == 0x7FFF ) {
        if ( aSig0 | aSig1 ) {
            return commonNaNToFloatx80(float128ToCommonNaN(a, status), status);
        }
        return packFloatx80(aSign, floatx80_infinity_high,
                                   floatx80_infinity_low);
    }
    if ( aExp == 0 ) {
        if ( ( aSig0 | aSig1 ) == 0 ) return packFloatx80( aSign, 0, 0 );
        normalizeFloat128Subnormal( aSig0, aSig1, &aExp, &aSig0, &aSig1 );
    }
    else {
        aSig0 |= UINT64_C(0x0001000000000000);
    }
    shortShift128Left( aSig0, aSig1, 15, &aSig0, &aSig1 );
    return roundAndPackFloatx80(80, aSign, aExp, aSig0, aSig1, status);

}

/*----------------------------------------------------------------------------
| Rounds the quadruple-precision floating-point value `a' to an integer, and
| returns the result as a quadruple-precision floating-point value.  The
| operation is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 float128_round_to_int(float128 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t lastBitMask, roundBitsMask;
    float128 z;

    aExp = extractFloat128Exp( a );
    if ( 0x402F <= aExp ) {
        if ( 0x406F <= aExp ) {
            if (    ( aExp == 0x7FFF )
                 && ( extractFloat128Frac0( a ) | extractFloat128Frac1( a ) )
               ) {
                return propagateFloat128NaN(a, a, status);
            }
            return a;
        }
        lastBitMask = 1;
        lastBitMask = ( lastBitMask<<( 0x406E - aExp ) )<<1;
        roundBitsMask = lastBitMask - 1;
        z = a;
        switch (status->float_rounding_mode) {
        case float_round_nearest_even:
            if ( lastBitMask ) {
                add128( z.high, z.low, 0, lastBitMask>>1, &z.high, &z.low );
                if ( ( z.low & roundBitsMask ) == 0 ) z.low &= ~ lastBitMask;
            }
            else {
                if ( (int64_t) z.low < 0 ) {
                    ++z.high;
                    if ( (uint64_t) ( z.low<<1 ) == 0 ) z.high &= ~1;
                }
            }
            break;
        case float_round_ties_away:
            if (lastBitMask) {
                add128(z.high, z.low, 0, lastBitMask >> 1, &z.high, &z.low);
            } else {
                if ((int64_t) z.low < 0) {
                    ++z.high;
                }
            }
            break;
        case float_round_to_zero:
            break;
        case float_round_up:
            if (!extractFloat128Sign(z)) {
                add128(z.high, z.low, 0, roundBitsMask, &z.high, &z.low);
            }
            break;
        case float_round_down:
            if (extractFloat128Sign(z)) {
                add128(z.high, z.low, 0, roundBitsMask, &z.high, &z.low);
            }
            break;
        case float_round_to_odd:
            /*
             * Note that if lastBitMask == 0, the last bit is the lsb
             * of high, and roundBitsMask == -1.
             */
            if ((lastBitMask ? z.low & lastBitMask : z.high & 1) == 0) {
                add128(z.high, z.low, 0, roundBitsMask, &z.high, &z.low);
            }
            break;
        default:
            abort();
        }
        z.low &= ~ roundBitsMask;
    }
    else {
        if ( aExp < 0x3FFF ) {
            if ( ( ( (uint64_t) ( a.high<<1 ) ) | a.low ) == 0 ) return a;
            status->float_exception_flags |= float_flag_inexact;
            aSign = extractFloat128Sign( a );
            switch (status->float_rounding_mode) {
            case float_round_nearest_even:
                if (    ( aExp == 0x3FFE )
                     && (   extractFloat128Frac0( a )
                          | extractFloat128Frac1( a ) )
                   ) {
                    return packFloat128( aSign, 0x3FFF, 0, 0 );
                }
                break;
            case float_round_ties_away:
                if (aExp == 0x3FFE) {
                    return packFloat128(aSign, 0x3FFF, 0, 0);
                }
                break;
            case float_round_down:
                return
                      aSign ? packFloat128( 1, 0x3FFF, 0, 0 )
                    : packFloat128( 0, 0, 0, 0 );
            case float_round_up:
                return
                      aSign ? packFloat128( 1, 0, 0, 0 )
                    : packFloat128( 0, 0x3FFF, 0, 0 );

            case float_round_to_odd:
                return packFloat128(aSign, 0x3FFF, 0, 0);
            }
            return packFloat128( aSign, 0, 0, 0 );
        }
        lastBitMask = 1;
        lastBitMask <<= 0x402F - aExp;
        roundBitsMask = lastBitMask - 1;
        z.low = 0;
        z.high = a.high;
        switch (status->float_rounding_mode) {
        case float_round_nearest_even:
            z.high += lastBitMask>>1;
            if ( ( ( z.high & roundBitsMask ) | a.low ) == 0 ) {
                z.high &= ~ lastBitMask;
            }
            break;
        case float_round_ties_away:
            z.high += lastBitMask>>1;
            break;
        case float_round_to_zero:
            break;
        case float_round_up:
            if (!extractFloat128Sign(z)) {
                z.high |= ( a.low != 0 );
                z.high += roundBitsMask;
            }
            break;
        case float_round_down:
            if (extractFloat128Sign(z)) {
                z.high |= (a.low != 0);
                z.high += roundBitsMask;
            }
            break;
        case float_round_to_odd:
            if ((z.high & lastBitMask) == 0) {
                z.high |= (a.low != 0);
                z.high += roundBitsMask;
            }
            break;
        default:
            abort();
        }
        z.high &= ~ roundBitsMask;
    }
    if ( ( z.low != a.low ) || ( z.high != a.high ) ) {
        status->float_exception_flags |= float_flag_inexact;
    }
    return z;

}

/*----------------------------------------------------------------------------
| Returns the result of adding the absolute values of the quadruple-precision
| floating-point values `a' and `b'.  If `zSign' is 1, the sum is negated
| before being returned.  `zSign' is ignored if the result is a NaN.
| The addition is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static float128 addFloat128Sigs(float128 a, float128 b, flag zSign,
                                float_status *status)
{
    int32_t aExp, bExp, zExp;
    uint64_t aSig0, aSig1, bSig0, bSig1, zSig0, zSig1, zSig2;
    int32_t expDiff;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    bSig1 = extractFloat128Frac1( b );
    bSig0 = extractFloat128Frac0( b );
    bExp = extractFloat128Exp( b );
    expDiff = aExp - bExp;
    if ( 0 < expDiff ) {
        if ( aExp == 0x7FFF ) {
            if (aSig0 | aSig1) {
                return propagateFloat128NaN(a, b, status);
            }
            return a;
        }
        if ( bExp == 0 ) {
            --expDiff;
        }
        else {
            bSig0 |= UINT64_C(0x0001000000000000);
        }
        shift128ExtraRightJamming(
            bSig0, bSig1, 0, expDiff, &bSig0, &bSig1, &zSig2 );
        zExp = aExp;
    }
    else if ( expDiff < 0 ) {
        if ( bExp == 0x7FFF ) {
            if (bSig0 | bSig1) {
                return propagateFloat128NaN(a, b, status);
            }
            return packFloat128( zSign, 0x7FFF, 0, 0 );
        }
        if ( aExp == 0 ) {
            ++expDiff;
        }
        else {
            aSig0 |= UINT64_C(0x0001000000000000);
        }
        shift128ExtraRightJamming(
            aSig0, aSig1, 0, - expDiff, &aSig0, &aSig1, &zSig2 );
        zExp = bExp;
    }
    else {
        if ( aExp == 0x7FFF ) {
            if ( aSig0 | aSig1 | bSig0 | bSig1 ) {
                return propagateFloat128NaN(a, b, status);
            }
            return a;
        }
        add128( aSig0, aSig1, bSig0, bSig1, &zSig0, &zSig1 );
        if ( aExp == 0 ) {
            if (status->flush_to_zero) {
                if (zSig0 | zSig1) {
                    float_raise(float_flag_output_denormal, status);
                }
                return packFloat128(zSign, 0, 0, 0);
            }
            return packFloat128( zSign, 0, zSig0, zSig1 );
        }
        zSig2 = 0;
        zSig0 |= UINT64_C(0x0002000000000000);
        zExp = aExp;
        goto shiftRight1;
    }
    aSig0 |= UINT64_C(0x0001000000000000);
    add128( aSig0, aSig1, bSig0, bSig1, &zSig0, &zSig1 );
    --zExp;
    if ( zSig0 < UINT64_C(0x0002000000000000) ) goto roundAndPack;
    ++zExp;
 shiftRight1:
    shift128ExtraRightJamming(
        zSig0, zSig1, zSig2, 1, &zSig0, &zSig1, &zSig2 );
 roundAndPack:
    return roundAndPackFloat128(zSign, zExp, zSig0, zSig1, zSig2, status);

}

/*----------------------------------------------------------------------------
| Returns the result of subtracting the absolute values of the quadruple-
| precision floating-point values `a' and `b'.  If `zSign' is 1, the
| difference is negated before being returned.  `zSign' is ignored if the
| result is a NaN.  The subtraction is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static float128 subFloat128Sigs(float128 a, float128 b, flag zSign,
                                float_status *status)
{
    int32_t aExp, bExp, zExp;
    uint64_t aSig0, aSig1, bSig0, bSig1, zSig0, zSig1;
    int32_t expDiff;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    bSig1 = extractFloat128Frac1( b );
    bSig0 = extractFloat128Frac0( b );
    bExp = extractFloat128Exp( b );
    expDiff = aExp - bExp;
    shortShift128Left( aSig0, aSig1, 14, &aSig0, &aSig1 );
    shortShift128Left( bSig0, bSig1, 14, &bSig0, &bSig1 );
    if ( 0 < expDiff ) goto aExpBigger;
    if ( expDiff < 0 ) goto bExpBigger;
    if ( aExp == 0x7FFF ) {
        if ( aSig0 | aSig1 | bSig0 | bSig1 ) {
            return propagateFloat128NaN(a, b, status);
        }
        float_raise(float_flag_invalid, status);
        return float128_default_nan(status);
    }
    if ( aExp == 0 ) {
        aExp = 1;
        bExp = 1;
    }
    if ( bSig0 < aSig0 ) goto aBigger;
    if ( aSig0 < bSig0 ) goto bBigger;
    if ( bSig1 < aSig1 ) goto aBigger;
    if ( aSig1 < bSig1 ) goto bBigger;
    return packFloat128(status->float_rounding_mode == float_round_down,
                        0, 0, 0);
 bExpBigger:
    if ( bExp == 0x7FFF ) {
        if (bSig0 | bSig1) {
            return propagateFloat128NaN(a, b, status);
        }
        return packFloat128( zSign ^ 1, 0x7FFF, 0, 0 );
    }
    if ( aExp == 0 ) {
        ++expDiff;
    }
    else {
        aSig0 |= UINT64_C(0x4000000000000000);
    }
    shift128RightJamming( aSig0, aSig1, - expDiff, &aSig0, &aSig1 );
    bSig0 |= UINT64_C(0x4000000000000000);
 bBigger:
    sub128( bSig0, bSig1, aSig0, aSig1, &zSig0, &zSig1 );
    zExp = bExp;
    zSign ^= 1;
    goto normalizeRoundAndPack;
 aExpBigger:
    if ( aExp == 0x7FFF ) {
        if (aSig0 | aSig1) {
            return propagateFloat128NaN(a, b, status);
        }
        return a;
    }
    if ( bExp == 0 ) {
        --expDiff;
    }
    else {
        bSig0 |= UINT64_C(0x4000000000000000);
    }
    shift128RightJamming( bSig0, bSig1, expDiff, &bSig0, &bSig1 );
    aSig0 |= UINT64_C(0x4000000000000000);
 aBigger:
    sub128( aSig0, aSig1, bSig0, bSig1, &zSig0, &zSig1 );
    zExp = aExp;
 normalizeRoundAndPack:
    --zExp;
    return normalizeRoundAndPackFloat128(zSign, zExp - 14, zSig0, zSig1,
                                         status);

}

/*----------------------------------------------------------------------------
| Returns the result of adding the quadruple-precision floating-point values
| `a' and `b'.  The operation is performed according to the IEC/IEEE Standard
| for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 float128_add(float128 a, float128 b, float_status *status)
{
    flag aSign, bSign;

    aSign = extractFloat128Sign( a );
    bSign = extractFloat128Sign( b );
    if ( aSign == bSign ) {
        return addFloat128Sigs(a, b, aSign, status);
    }
    else {
        return subFloat128Sigs(a, b, aSign, status);
    }

}

/*----------------------------------------------------------------------------
| Returns the result of subtracting the quadruple-precision floating-point
| values `a' and `b'.  The operation is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 float128_sub(float128 a, float128 b, float_status *status)
{
    flag aSign, bSign;

    aSign = extractFloat128Sign( a );
    bSign = extractFloat128Sign( b );
    if ( aSign == bSign ) {
        return subFloat128Sigs(a, b, aSign, status);
    }
    else {
        return addFloat128Sigs(a, b, aSign, status);
    }

}

/*----------------------------------------------------------------------------
| Returns the result of multiplying the quadruple-precision floating-point
| values `a' and `b'.  The operation is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 float128_mul(float128 a, float128 b, float_status *status)
{
    flag aSign, bSign, zSign;
    int32_t aExp, bExp, zExp;
    uint64_t aSig0, aSig1, bSig0, bSig1, zSig0, zSig1, zSig2, zSig3;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    aSign = extractFloat128Sign( a );
    bSig1 = extractFloat128Frac1( b );
    bSig0 = extractFloat128Frac0( b );
    bExp = extractFloat128Exp( b );
    bSign = extractFloat128Sign( b );
    zSign = aSign ^ bSign;
    if ( aExp == 0x7FFF ) {
        if (    ( aSig0 | aSig1 )
             || ( ( bExp == 0x7FFF ) && ( bSig0 | bSig1 ) ) ) {
            return propagateFloat128NaN(a, b, status);
        }
        if ( ( bExp | bSig0 | bSig1 ) == 0 ) goto invalid;
        return packFloat128( zSign, 0x7FFF, 0, 0 );
    }
    if ( bExp == 0x7FFF ) {
        if (bSig0 | bSig1) {
            return propagateFloat128NaN(a, b, status);
        }
        if ( ( aExp | aSig0 | aSig1 ) == 0 ) {
 invalid:
            float_raise(float_flag_invalid, status);
            return float128_default_nan(status);
        }
        return packFloat128( zSign, 0x7FFF, 0, 0 );
    }
    if ( aExp == 0 ) {
        if ( ( aSig0 | aSig1 ) == 0 ) return packFloat128( zSign, 0, 0, 0 );
        normalizeFloat128Subnormal( aSig0, aSig1, &aExp, &aSig0, &aSig1 );
    }
    if ( bExp == 0 ) {
        if ( ( bSig0 | bSig1 ) == 0 ) return packFloat128( zSign, 0, 0, 0 );
        normalizeFloat128Subnormal( bSig0, bSig1, &bExp, &bSig0, &bSig1 );
    }
    zExp = aExp + bExp - 0x4000;
    aSig0 |= UINT64_C(0x0001000000000000);
    shortShift128Left( bSig0, bSig1, 16, &bSig0, &bSig1 );
    mul128To256( aSig0, aSig1, bSig0, bSig1, &zSig0, &zSig1, &zSig2, &zSig3 );
    add128( zSig0, zSig1, aSig0, aSig1, &zSig0, &zSig1 );
    zSig2 |= ( zSig3 != 0 );
    if (UINT64_C( 0x0002000000000000) <= zSig0 ) {
        shift128ExtraRightJamming(
            zSig0, zSig1, zSig2, 1, &zSig0, &zSig1, &zSig2 );
        ++zExp;
    }
    return roundAndPackFloat128(zSign, zExp, zSig0, zSig1, zSig2, status);

}

/*----------------------------------------------------------------------------
| Returns the result of dividing the quadruple-precision floating-point value
| `a' by the corresponding value `b'.  The operation is performed according to
| the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 float128_div(float128 a, float128 b, float_status *status)
{
    flag aSign, bSign, zSign;
    int32_t aExp, bExp, zExp;
    uint64_t aSig0, aSig1, bSig0, bSig1, zSig0, zSig1, zSig2;
    uint64_t rem0, rem1, rem2, rem3, term0, term1, term2, term3;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    aSign = extractFloat128Sign( a );
    bSig1 = extractFloat128Frac1( b );
    bSig0 = extractFloat128Frac0( b );
    bExp = extractFloat128Exp( b );
    bSign = extractFloat128Sign( b );
    zSign = aSign ^ bSign;
    if ( aExp == 0x7FFF ) {
        if (aSig0 | aSig1) {
            return propagateFloat128NaN(a, b, status);
        }
        if ( bExp == 0x7FFF ) {
            if (bSig0 | bSig1) {
                return propagateFloat128NaN(a, b, status);
            }
            goto invalid;
        }
        return packFloat128( zSign, 0x7FFF, 0, 0 );
    }
    if ( bExp == 0x7FFF ) {
        if (bSig0 | bSig1) {
            return propagateFloat128NaN(a, b, status);
        }
        return packFloat128( zSign, 0, 0, 0 );
    }
    if ( bExp == 0 ) {
        if ( ( bSig0 | bSig1 ) == 0 ) {
            if ( ( aExp | aSig0 | aSig1 ) == 0 ) {
 invalid:
                float_raise(float_flag_invalid, status);
                return float128_default_nan(status);
            }
            float_raise(float_flag_divbyzero, status);
            return packFloat128( zSign, 0x7FFF, 0, 0 );
        }
        normalizeFloat128Subnormal( bSig0, bSig1, &bExp, &bSig0, &bSig1 );
    }
    if ( aExp == 0 ) {
        if ( ( aSig0 | aSig1 ) == 0 ) return packFloat128( zSign, 0, 0, 0 );
        normalizeFloat128Subnormal( aSig0, aSig1, &aExp, &aSig0, &aSig1 );
    }
    zExp = aExp - bExp + 0x3FFD;
    shortShift128Left(
        aSig0 | UINT64_C(0x0001000000000000), aSig1, 15, &aSig0, &aSig1 );
    shortShift128Left(
        bSig0 | UINT64_C(0x0001000000000000), bSig1, 15, &bSig0, &bSig1 );
    if ( le128( bSig0, bSig1, aSig0, aSig1 ) ) {
        shift128Right( aSig0, aSig1, 1, &aSig0, &aSig1 );
        ++zExp;
    }
    zSig0 = estimateDiv128To64( aSig0, aSig1, bSig0 );
    mul128By64To192( bSig0, bSig1, zSig0, &term0, &term1, &term2 );
    sub192( aSig0, aSig1, 0, term0, term1, term2, &rem0, &rem1, &rem2 );
    while ( (int64_t) rem0 < 0 ) {
        --zSig0;
        add192( rem0, rem1, rem2, 0, bSig0, bSig1, &rem0, &rem1, &rem2 );
    }
    zSig1 = estimateDiv128To64( rem1, rem2, bSig0 );
    if ( ( zSig1 & 0x3FFF ) <= 4 ) {
        mul128By64To192( bSig0, bSig1, zSig1, &term1, &term2, &term3 );
        sub192( rem1, rem2, 0, term1, term2, term3, &rem1, &rem2, &rem3 );
        while ( (int64_t) rem1 < 0 ) {
            --zSig1;
            add192( rem1, rem2, rem3, 0, bSig0, bSig1, &rem1, &rem2, &rem3 );
        }
        zSig1 |= ( ( rem1 | rem2 | rem3 ) != 0 );
    }
    shift128ExtraRightJamming( zSig0, zSig1, 0, 15, &zSig0, &zSig1, &zSig2 );
    return roundAndPackFloat128(zSign, zExp, zSig0, zSig1, zSig2, status);

}

/*----------------------------------------------------------------------------
| Returns the remainder of the quadruple-precision floating-point value `a'
| with respect to the corresponding value `b'.  The operation is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 float128_rem(float128 a, float128 b, float_status *status)
{
    flag aSign, zSign;
    int32_t aExp, bExp, expDiff;
    uint64_t aSig0, aSig1, bSig0, bSig1, q, term0, term1, term2;
    uint64_t allZero, alternateASig0, alternateASig1, sigMean1;
    int64_t sigMean0;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    aSign = extractFloat128Sign( a );
    bSig1 = extractFloat128Frac1( b );
    bSig0 = extractFloat128Frac0( b );
    bExp = extractFloat128Exp( b );
    if ( aExp == 0x7FFF ) {
        if (    ( aSig0 | aSig1 )
             || ( ( bExp == 0x7FFF ) && ( bSig0 | bSig1 ) ) ) {
            return propagateFloat128NaN(a, b, status);
        }
        goto invalid;
    }
    if ( bExp == 0x7FFF ) {
        if (bSig0 | bSig1) {
            return propagateFloat128NaN(a, b, status);
        }
        return a;
    }
    if ( bExp == 0 ) {
        if ( ( bSig0 | bSig1 ) == 0 ) {
 invalid:
            float_raise(float_flag_invalid, status);
            return float128_default_nan(status);
        }
        normalizeFloat128Subnormal( bSig0, bSig1, &bExp, &bSig0, &bSig1 );
    }
    if ( aExp == 0 ) {
        if ( ( aSig0 | aSig1 ) == 0 ) return a;
        normalizeFloat128Subnormal( aSig0, aSig1, &aExp, &aSig0, &aSig1 );
    }
    expDiff = aExp - bExp;
    if ( expDiff < -1 ) return a;
    shortShift128Left(
        aSig0 | UINT64_C(0x0001000000000000),
        aSig1,
        15 - ( expDiff < 0 ),
        &aSig0,
        &aSig1
    );
    shortShift128Left(
        bSig0 | UINT64_C(0x0001000000000000), bSig1, 15, &bSig0, &bSig1 );
    q = le128( bSig0, bSig1, aSig0, aSig1 );
    if ( q ) sub128( aSig0, aSig1, bSig0, bSig1, &aSig0, &aSig1 );
    expDiff -= 64;
    while ( 0 < expDiff ) {
        q = estimateDiv128To64( aSig0, aSig1, bSig0 );
        q = ( 4 < q ) ? q - 4 : 0;
        mul128By64To192( bSig0, bSig1, q, &term0, &term1, &term2 );
        shortShift192Left( term0, term1, term2, 61, &term1, &term2, &allZero );
        shortShift128Left( aSig0, aSig1, 61, &aSig0, &allZero );
        sub128( aSig0, 0, term1, term2, &aSig0, &aSig1 );
        expDiff -= 61;
    }
    if ( -64 < expDiff ) {
        q = estimateDiv128To64( aSig0, aSig1, bSig0 );
        q = ( 4 < q ) ? q - 4 : 0;
        q >>= - expDiff;
        shift128Right( bSig0, bSig1, 12, &bSig0, &bSig1 );
        expDiff += 52;
        if ( expDiff < 0 ) {
            shift128Right( aSig0, aSig1, - expDiff, &aSig0, &aSig1 );
        }
        else {
            shortShift128Left( aSig0, aSig1, expDiff, &aSig0, &aSig1 );
        }
        mul128By64To192( bSig0, bSig1, q, &term0, &term1, &term2 );
        sub128( aSig0, aSig1, term1, term2, &aSig0, &aSig1 );
    }
    else {
        shift128Right( aSig0, aSig1, 12, &aSig0, &aSig1 );
        shift128Right( bSig0, bSig1, 12, &bSig0, &bSig1 );
    }
    do {
        alternateASig0 = aSig0;
        alternateASig1 = aSig1;
        ++q;
        sub128( aSig0, aSig1, bSig0, bSig1, &aSig0, &aSig1 );
    } while ( 0 <= (int64_t) aSig0 );
    add128(
        aSig0, aSig1, alternateASig0, alternateASig1, (uint64_t *)&sigMean0, &sigMean1 );
    if (    ( sigMean0 < 0 )
         || ( ( ( sigMean0 | sigMean1 ) == 0 ) && ( q & 1 ) ) ) {
        aSig0 = alternateASig0;
        aSig1 = alternateASig1;
    }
    zSign = ( (int64_t) aSig0 < 0 );
    if ( zSign ) sub128( 0, 0, aSig0, aSig1, &aSig0, &aSig1 );
    return normalizeRoundAndPackFloat128(aSign ^ zSign, bExp - 4, aSig0, aSig1,
                                         status);
}

/*----------------------------------------------------------------------------
| Returns the square root of the quadruple-precision floating-point value `a'.
| The operation is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 float128_sqrt(float128 a, float_status *status)
{
    flag aSign;
    int32_t aExp, zExp;
    uint64_t aSig0, aSig1, zSig0, zSig1, zSig2, doubleZSig0;
    uint64_t rem0, rem1, rem2, rem3, term0, term1, term2, term3;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    aSign = extractFloat128Sign( a );
    if ( aExp == 0x7FFF ) {
        if (aSig0 | aSig1) {
            return propagateFloat128NaN(a, a, status);
        }
        if ( ! aSign ) return a;
        goto invalid;
    }
    if ( aSign ) {
        if ( ( aExp | aSig0 | aSig1 ) == 0 ) return a;
 invalid:
        float_raise(float_flag_invalid, status);
        return float128_default_nan(status);
    }
    if ( aExp == 0 ) {
        if ( ( aSig0 | aSig1 ) == 0 ) return packFloat128( 0, 0, 0, 0 );
        normalizeFloat128Subnormal( aSig0, aSig1, &aExp, &aSig0, &aSig1 );
    }
    zExp = ( ( aExp - 0x3FFF )>>1 ) + 0x3FFE;
    aSig0 |= UINT64_C(0x0001000000000000);
    zSig0 = estimateSqrt32( aExp, aSig0>>17 );
    shortShift128Left( aSig0, aSig1, 13 - ( aExp & 1 ), &aSig0, &aSig1 );
    zSig0 = estimateDiv128To64( aSig0, aSig1, zSig0<<32 ) + ( zSig0<<30 );
    doubleZSig0 = zSig0<<1;
    mul64To128( zSig0, zSig0, &term0, &term1 );
    sub128( aSig0, aSig1, term0, term1, &rem0, &rem1 );
    while ( (int64_t) rem0 < 0 ) {
        --zSig0;
        doubleZSig0 -= 2;
        add128( rem0, rem1, zSig0>>63, doubleZSig0 | 1, &rem0, &rem1 );
    }
    zSig1 = estimateDiv128To64( rem1, 0, doubleZSig0 );
    if ( ( zSig1 & 0x1FFF ) <= 5 ) {
        if ( zSig1 == 0 ) zSig1 = 1;
        mul64To128( doubleZSig0, zSig1, &term1, &term2 );
        sub128( rem1, 0, term1, term2, &rem1, &rem2 );
        mul64To128( zSig1, zSig1, &term2, &term3 );
        sub192( rem1, rem2, 0, 0, term2, term3, &rem1, &rem2, &rem3 );
        while ( (int64_t) rem1 < 0 ) {
            --zSig1;
            shortShift128Left( 0, zSig1, 1, &term2, &term3 );
            term3 |= 1;
            term2 |= doubleZSig0;
            add192( rem1, rem2, rem3, 0, term2, term3, &rem1, &rem2, &rem3 );
        }
        zSig1 |= ( ( rem1 | rem2 | rem3 ) != 0 );
    }
    shift128ExtraRightJamming( zSig0, zSig1, 0, 14, &zSig0, &zSig1, &zSig2 );
    return roundAndPackFloat128(0, zExp, zSig0, zSig1, zSig2, status);

}

/*----------------------------------------------------------------------------
| Returns 1 if the quadruple-precision floating-point value `a' is equal to
| the corresponding value `b', and 0 otherwise.  The invalid exception is
| raised if either operand is a NaN.  Otherwise, the comparison is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float128_eq(float128 a, float128 b, float_status *status)
{

    if (    (    ( extractFloat128Exp( a ) == 0x7FFF )
              && ( extractFloat128Frac0( a ) | extractFloat128Frac1( a ) ) )
         || (    ( extractFloat128Exp( b ) == 0x7FFF )
              && ( extractFloat128Frac0( b ) | extractFloat128Frac1( b ) ) )
       ) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    return
           ( a.low == b.low )
        && (    ( a.high == b.high )
             || (    ( a.low == 0 )
                  && ( (uint64_t) ( ( a.high | b.high )<<1 ) == 0 ) )
           );

}

/*----------------------------------------------------------------------------
| Returns 1 if the quadruple-precision floating-point value `a' is less than
| or equal to the corresponding value `b', and 0 otherwise.  The invalid
| exception is raised if either operand is a NaN.  The comparison is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float128_le(float128 a, float128 b, float_status *status)
{
    flag aSign, bSign;

    if (    (    ( extractFloat128Exp( a ) == 0x7FFF )
              && ( extractFloat128Frac0( a ) | extractFloat128Frac1( a ) ) )
         || (    ( extractFloat128Exp( b ) == 0x7FFF )
              && ( extractFloat128Frac0( b ) | extractFloat128Frac1( b ) ) )
       ) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    aSign = extractFloat128Sign( a );
    bSign = extractFloat128Sign( b );
    if ( aSign != bSign ) {
        return
               aSign
            || (    ( ( (uint64_t) ( ( a.high | b.high )<<1 ) ) | a.low | b.low )
                 == 0 );
    }
    return
          aSign ? le128( b.high, b.low, a.high, a.low )
        : le128( a.high, a.low, b.high, b.low );

}

/*----------------------------------------------------------------------------
| Returns 1 if the quadruple-precision floating-point value `a' is less than
| the corresponding value `b', and 0 otherwise.  The invalid exception is
| raised if either operand is a NaN.  The comparison is performed according
| to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float128_lt(float128 a, float128 b, float_status *status)
{
    flag aSign, bSign;

    if (    (    ( extractFloat128Exp( a ) == 0x7FFF )
              && ( extractFloat128Frac0( a ) | extractFloat128Frac1( a ) ) )
         || (    ( extractFloat128Exp( b ) == 0x7FFF )
              && ( extractFloat128Frac0( b ) | extractFloat128Frac1( b ) ) )
       ) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    aSign = extractFloat128Sign( a );
    bSign = extractFloat128Sign( b );
    if ( aSign != bSign ) {
        return
               aSign
            && (    ( ( (uint64_t) ( ( a.high | b.high )<<1 ) ) | a.low | b.low )
                 != 0 );
    }
    return
          aSign ? lt128( b.high, b.low, a.high, a.low )
        : lt128( a.high, a.low, b.high, b.low );

}

/*----------------------------------------------------------------------------
| Returns 1 if the quadruple-precision floating-point values `a' and `b' cannot
| be compared, and 0 otherwise.  The invalid exception is raised if either
| operand is a NaN. The comparison is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float128_unordered(float128 a, float128 b, float_status *status)
{
    if (    (    ( extractFloat128Exp( a ) == 0x7FFF )
              && ( extractFloat128Frac0( a ) | extractFloat128Frac1( a ) ) )
         || (    ( extractFloat128Exp( b ) == 0x7FFF )
              && ( extractFloat128Frac0( b ) | extractFloat128Frac1( b ) ) )
       ) {
        float_raise(float_flag_invalid, status);
        return 1;
    }
    return 0;
}

/*----------------------------------------------------------------------------
| Returns 1 if the quadruple-precision floating-point value `a' is equal to
| the corresponding value `b', and 0 otherwise.  Quiet NaNs do not cause an
| exception.  The comparison is performed according to the IEC/IEEE Standard
| for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float128_eq_quiet(float128 a, float128 b, float_status *status)
{

    if (    (    ( extractFloat128Exp( a ) == 0x7FFF )
              && ( extractFloat128Frac0( a ) | extractFloat128Frac1( a ) ) )
         || (    ( extractFloat128Exp( b ) == 0x7FFF )
              && ( extractFloat128Frac0( b ) | extractFloat128Frac1( b ) ) )
       ) {
        if (float128_is_signaling_nan(a, status)
         || float128_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 0;
    }
    return
           ( a.low == b.low )
        && (    ( a.high == b.high )
             || (    ( a.low == 0 )
                  && ( (uint64_t) ( ( a.high | b.high )<<1 ) == 0 ) )
           );

}

/*----------------------------------------------------------------------------
| Returns 1 if the quadruple-precision floating-point value `a' is less than
| or equal to the corresponding value `b', and 0 otherwise.  Quiet NaNs do not
| cause an exception.  Otherwise, the comparison is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float128_le_quiet(float128 a, float128 b, float_status *status)
{
    flag aSign, bSign;

    if (    (    ( extractFloat128Exp( a ) == 0x7FFF )
              && ( extractFloat128Frac0( a ) | extractFloat128Frac1( a ) ) )
         || (    ( extractFloat128Exp( b ) == 0x7FFF )
              && ( extractFloat128Frac0( b ) | extractFloat128Frac1( b ) ) )
       ) {
        if (float128_is_signaling_nan(a, status)
         || float128_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 0;
    }
    aSign = extractFloat128Sign( a );
    bSign = extractFloat128Sign( b );
    if ( aSign != bSign ) {
        return
               aSign
            || (    ( ( (uint64_t) ( ( a.high | b.high )<<1 ) ) | a.low | b.low )
                 == 0 );
    }
    return
          aSign ? le128( b.high, b.low, a.high, a.low )
        : le128( a.high, a.low, b.high, b.low );

}

/*----------------------------------------------------------------------------
| Returns 1 if the quadruple-precision floating-point value `a' is less than
| the corresponding value `b', and 0 otherwise.  Quiet NaNs do not cause an
| exception.  Otherwise, the comparison is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float128_lt_quiet(float128 a, float128 b, float_status *status)
{
    flag aSign, bSign;

    if (    (    ( extractFloat128Exp( a ) == 0x7FFF )
              && ( extractFloat128Frac0( a ) | extractFloat128Frac1( a ) ) )
         || (    ( extractFloat128Exp( b ) == 0x7FFF )
              && ( extractFloat128Frac0( b ) | extractFloat128Frac1( b ) ) )
       ) {
        if (float128_is_signaling_nan(a, status)
         || float128_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 0;
    }
    aSign = extractFloat128Sign( a );
    bSign = extractFloat128Sign( b );
    if ( aSign != bSign ) {
        return
               aSign
            && (    ( ( (uint64_t) ( ( a.high | b.high )<<1 ) ) | a.low | b.low )
                 != 0 );
    }
    return
          aSign ? lt128( b.high, b.low, a.high, a.low )
        : lt128( a.high, a.low, b.high, b.low );

}

/*----------------------------------------------------------------------------
| Returns 1 if the quadruple-precision floating-point values `a' and `b' cannot
| be compared, and 0 otherwise.  Quiet NaNs do not cause an exception.  The
| comparison is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float128_unordered_quiet(float128 a, float128 b, float_status *status)
{
    if (    (    ( extractFloat128Exp( a ) == 0x7FFF )
              && ( extractFloat128Frac0( a ) | extractFloat128Frac1( a ) ) )
         || (    ( extractFloat128Exp( b ) == 0x7FFF )
              && ( extractFloat128Frac0( b ) | extractFloat128Frac1( b ) ) )
       ) {
        if (float128_is_signaling_nan(a, status)
         || float128_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 1;
    }
    return 0;
}

static inline int floatx80_compare_internal(floatx80 a, floatx80 b,
                                            int is_quiet, float_status *status)
{
    flag aSign, bSign;

    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)) {
        float_raise(float_flag_invalid, status);
        return float_relation_unordered;
    }
    if (( ( extractFloatx80Exp( a ) == 0x7fff ) &&
          ( extractFloatx80Frac( a )<<1 ) ) ||
        ( ( extractFloatx80Exp( b ) == 0x7fff ) &&
          ( extractFloatx80Frac( b )<<1 ) )) {
        if (!is_quiet ||
            floatx80_is_signaling_nan(a, status) ||
            floatx80_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return float_relation_unordered;
    }
    aSign = extractFloatx80Sign( a );
    bSign = extractFloatx80Sign( b );
    if ( aSign != bSign ) {

        if ( ( ( (uint16_t) ( ( a.high | b.high ) << 1 ) ) == 0) &&
             ( ( a.low | b.low ) == 0 ) ) {
            /* zero case */
            return float_relation_equal;
        } else {
            return 1 - (2 * aSign);
        }
    } else {
        if (a.low == b.low && a.high == b.high) {
            return float_relation_equal;
        } else {
            return 1 - 2 * (aSign ^ ( lt128( a.high, a.low, b.high, b.low ) ));
        }
    }
}

int floatx80_compare(floatx80 a, floatx80 b, float_status *status)
{
    return floatx80_compare_internal(a, b, 0, status);
}

int floatx80_compare_quiet(floatx80 a, floatx80 b, float_status *status)
{
    return floatx80_compare_internal(a, b, 1, status);
}

static inline int float128_compare_internal(float128 a, float128 b,
                                            int is_quiet, float_status *status)
{
    flag aSign, bSign;

    if (( ( extractFloat128Exp( a ) == 0x7fff ) &&
          ( extractFloat128Frac0( a ) | extractFloat128Frac1( a ) ) ) ||
        ( ( extractFloat128Exp( b ) == 0x7fff ) &&
          ( extractFloat128Frac0( b ) | extractFloat128Frac1( b ) ) )) {
        if (!is_quiet ||
            float128_is_signaling_nan(a, status) ||
            float128_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return float_relation_unordered;
    }
    aSign = extractFloat128Sign( a );
    bSign = extractFloat128Sign( b );
    if ( aSign != bSign ) {
        if ( ( ( ( a.high | b.high )<<1 ) | a.low | b.low ) == 0 ) {
            /* zero case */
            return float_relation_equal;
        } else {
            return 1 - (2 * aSign);
        }
    } else {
        if (a.low == b.low && a.high == b.high) {
            return float_relation_equal;
        } else {
            return 1 - 2 * (aSign ^ ( lt128( a.high, a.low, b.high, b.low ) ));
        }
    }
}

int float128_compare(float128 a, float128 b, float_status *status)
{
    return float128_compare_internal(a, b, 0, status);
}

int float128_compare_quiet(float128 a, float128 b, float_status *status)
{
    return float128_compare_internal(a, b, 1, status);
}

floatx80 floatx80_scalbn(floatx80 a, int n, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    if (floatx80_invalid_encoding(a)) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }
    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );

    if ( aExp == 0x7FFF ) {
        if ( aSig<<1 ) {
            return propagateFloatx80NaN(a, a, status);
        }
        return a;
    }

    if (aExp == 0) {
        if (aSig == 0) {
            return a;
        }
        aExp++;
    }

    if (n > 0x10000) {
        n = 0x10000;
    } else if (n < -0x10000) {
        n = -0x10000;
    }

    aExp += n;
    return normalizeRoundAndPackFloatx80(status->floatx80_rounding_precision,
                                         aSign, aExp, aSig, 0, status);
}

float128 float128_scalbn(float128 a, int n, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig0, aSig1;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    aSign = extractFloat128Sign( a );
    if ( aExp == 0x7FFF ) {
        if ( aSig0 | aSig1 ) {
            return propagateFloat128NaN(a, a, status);
        }
        return a;
    }
    if (aExp != 0) {
        aSig0 |= UINT64_C(0x0001000000000000);
    } else if (aSig0 == 0 && aSig1 == 0) {
        return a;
    } else {
        aExp++;
    }

    if (n > 0x10000) {
        n = 0x10000;
    } else if (n < -0x10000) {
        n = -0x10000;
    }

    aExp += n - 1;
    return normalizeRoundAndPackFloat128( aSign, aExp, aSig0, aSig1
                                         , status);

}

static void __attribute__((constructor)) softfloat_init(void)
{
    union_float64 ua, ub, uc, ur;

    if (QEMU_NO_HARDFLOAT) {
        return;
    }
    /*
     * Test that the host's FMA is not obviously broken. For example,
     * glibc < 2.23 can perform an incorrect FMA on certain hosts; see
     *   https://sourceware.org/bugzilla/show_bug.cgi?id=13304
     */
    ua.s = 0x0020000000000001ULL;
    ub.s = 0x3ca0000000000000ULL;
    uc.s = 0x0020000000000000ULL;
    ur.h = fma(ua.h, ub.h, uc.h);
    if (ur.s != 0x0020000000000001ULL) {
        force_soft_fma = true;
    }
}
