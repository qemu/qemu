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
This C header file is part of the SoftFloat IEC/IEEE Floating-point
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

#ifndef SOFTFLOAT_H
#define SOFTFLOAT_H

#if defined(CONFIG_SOLARIS) && defined(CONFIG_NEEDS_LIBSUNMATH)
#include <sunmath.h>
#endif


/* This 'flag' type must be able to hold at least 0 and 1. It should
 * probably be replaced with 'bool' but the uses would need to be audited
 * to check that they weren't accidentally relying on it being a larger type.
 */
typedef uint8_t flag;

#define LIT64( a ) a##LL

/*----------------------------------------------------------------------------
| Software IEC/IEEE floating-point ordering relations
*----------------------------------------------------------------------------*/
enum {
    float_relation_less      = -1,
    float_relation_equal     =  0,
    float_relation_greater   =  1,
    float_relation_unordered =  2
};

/*----------------------------------------------------------------------------
| Software IEC/IEEE floating-point types.
*----------------------------------------------------------------------------*/
/* Use structures for soft-float types.  This prevents accidentally mixing
   them with native int/float types.  A sufficiently clever compiler and
   sane ABI should be able to see though these structs.  However
   x86/gcc 3.x seems to struggle a bit, so leave them disabled by default.  */
//#define USE_SOFTFLOAT_STRUCT_TYPES
#ifdef USE_SOFTFLOAT_STRUCT_TYPES
typedef struct {
    uint16_t v;
} float16;
#define float16_val(x) (((float16)(x)).v)
#define make_float16(x) __extension__ ({ float16 f16_val = {x}; f16_val; })
#define const_float16(x) { x }
typedef struct {
    uint32_t v;
} float32;
/* The cast ensures an error if the wrong type is passed.  */
#define float32_val(x) (((float32)(x)).v)
#define make_float32(x) __extension__ ({ float32 f32_val = {x}; f32_val; })
#define const_float32(x) { x }
typedef struct {
    uint64_t v;
} float64;
#define float64_val(x) (((float64)(x)).v)
#define make_float64(x) __extension__ ({ float64 f64_val = {x}; f64_val; })
#define const_float64(x) { x }
#else
typedef uint16_t float16;
typedef uint32_t float32;
typedef uint64_t float64;
#define float16_val(x) (x)
#define float32_val(x) (x)
#define float64_val(x) (x)
#define make_float16(x) (x)
#define make_float32(x) (x)
#define make_float64(x) (x)
#define const_float16(x) (x)
#define const_float32(x) (x)
#define const_float64(x) (x)
#endif
typedef struct {
    uint64_t low;
    uint16_t high;
} floatx80;
#define make_floatx80(exp, mant) ((floatx80) { mant, exp })
#define make_floatx80_init(exp, mant) { .low = mant, .high = exp }
typedef struct {
#ifdef HOST_WORDS_BIGENDIAN
    uint64_t high, low;
#else
    uint64_t low, high;
#endif
} float128;
#define make_float128(high_, low_) ((float128) { .high = high_, .low = low_ })
#define make_float128_init(high_, low_) { .high = high_, .low = low_ }

/*----------------------------------------------------------------------------
| Software IEC/IEEE floating-point underflow tininess-detection mode.
*----------------------------------------------------------------------------*/
enum {
    float_tininess_after_rounding  = 0,
    float_tininess_before_rounding = 1
};

/*----------------------------------------------------------------------------
| Software IEC/IEEE floating-point rounding mode.
*----------------------------------------------------------------------------*/
enum {
    float_round_nearest_even = 0,
    float_round_down         = 1,
    float_round_up           = 2,
    float_round_to_zero      = 3,
    float_round_ties_away    = 4,
    /* Not an IEEE rounding mode: round to the closest odd mantissa value */
    float_round_to_odd       = 5,
};

/*----------------------------------------------------------------------------
| Software IEC/IEEE floating-point exception flags.
*----------------------------------------------------------------------------*/
enum {
    float_flag_invalid   =  1,
    float_flag_divbyzero =  4,
    float_flag_overflow  =  8,
    float_flag_underflow = 16,
    float_flag_inexact   = 32,
    float_flag_input_denormal = 64,
    float_flag_output_denormal = 128
};

typedef struct float_status {
    signed char float_detect_tininess;
    signed char float_rounding_mode;
    uint8_t     float_exception_flags;
    signed char floatx80_rounding_precision;
    /* should denormalised results go to zero and set the inexact flag? */
    flag flush_to_zero;
    /* should denormalised inputs go to zero and set the input_denormal flag? */
    flag flush_inputs_to_zero;
    flag default_nan_mode;
    flag snan_bit_is_one;
} float_status;

static inline void set_float_detect_tininess(int val, float_status *status)
{
    status->float_detect_tininess = val;
}
static inline void set_float_rounding_mode(int val, float_status *status)
{
    status->float_rounding_mode = val;
}
static inline void set_float_exception_flags(int val, float_status *status)
{
    status->float_exception_flags = val;
}
static inline void set_floatx80_rounding_precision(int val,
                                                   float_status *status)
{
    status->floatx80_rounding_precision = val;
}
static inline void set_flush_to_zero(flag val, float_status *status)
{
    status->flush_to_zero = val;
}
static inline void set_flush_inputs_to_zero(flag val, float_status *status)
{
    status->flush_inputs_to_zero = val;
}
static inline void set_default_nan_mode(flag val, float_status *status)
{
    status->default_nan_mode = val;
}
static inline void set_snan_bit_is_one(flag val, float_status *status)
{
    status->snan_bit_is_one = val;
}
static inline int get_float_detect_tininess(float_status *status)
{
    return status->float_detect_tininess;
}
static inline int get_float_rounding_mode(float_status *status)
{
    return status->float_rounding_mode;
}
static inline int get_float_exception_flags(float_status *status)
{
    return status->float_exception_flags;
}
static inline int get_floatx80_rounding_precision(float_status *status)
{
    return status->floatx80_rounding_precision;
}
static inline flag get_flush_to_zero(float_status *status)
{
    return status->flush_to_zero;
}
static inline flag get_flush_inputs_to_zero(float_status *status)
{
    return status->flush_inputs_to_zero;
}
static inline flag get_default_nan_mode(float_status *status)
{
    return status->default_nan_mode;
}

/*----------------------------------------------------------------------------
| Routine to raise any or all of the software IEC/IEEE floating-point
| exception flags.
*----------------------------------------------------------------------------*/
void float_raise(uint8_t flags, float_status *status);

/*----------------------------------------------------------------------------
| If `a' is denormal and we are in flush-to-zero mode then set the
| input-denormal exception and return zero. Otherwise just return the value.
*----------------------------------------------------------------------------*/
float32 float32_squash_input_denormal(float32 a, float_status *status);
float64 float64_squash_input_denormal(float64 a, float_status *status);

/*----------------------------------------------------------------------------
| Options to indicate which negations to perform in float*_muladd()
| Using these differs from negating an input or output before calling
| the muladd function in that this means that a NaN doesn't have its
| sign bit inverted before it is propagated.
| We also support halving the result before rounding, as a special
| case to support the ARM fused-sqrt-step instruction FRSQRTS.
*----------------------------------------------------------------------------*/
enum {
    float_muladd_negate_c = 1,
    float_muladd_negate_product = 2,
    float_muladd_negate_result = 4,
    float_muladd_halve_result = 8,
};

/*----------------------------------------------------------------------------
| Software IEC/IEEE integer-to-floating-point conversion routines.
*----------------------------------------------------------------------------*/
float32 int32_to_float32(int32_t, float_status *status);
float64 int32_to_float64(int32_t, float_status *status);
float32 uint32_to_float32(uint32_t, float_status *status);
float64 uint32_to_float64(uint32_t, float_status *status);
floatx80 int32_to_floatx80(int32_t, float_status *status);
float128 int32_to_float128(int32_t, float_status *status);
float32 int64_to_float32(int64_t, float_status *status);
float64 int64_to_float64(int64_t, float_status *status);
floatx80 int64_to_floatx80(int64_t, float_status *status);
float128 int64_to_float128(int64_t, float_status *status);
float32 uint64_to_float32(uint64_t, float_status *status);
float64 uint64_to_float64(uint64_t, float_status *status);
float128 uint64_to_float128(uint64_t, float_status *status);

/* We provide the int16 versions for symmetry of API with float-to-int */
static inline float32 int16_to_float32(int16_t v, float_status *status)
{
    return int32_to_float32(v, status);
}

static inline float32 uint16_to_float32(uint16_t v, float_status *status)
{
    return uint32_to_float32(v, status);
}

static inline float64 int16_to_float64(int16_t v, float_status *status)
{
    return int32_to_float64(v, status);
}

static inline float64 uint16_to_float64(uint16_t v, float_status *status)
{
    return uint32_to_float64(v, status);
}

/*----------------------------------------------------------------------------
| Software half-precision conversion routines.
*----------------------------------------------------------------------------*/
float16 float32_to_float16(float32, flag, float_status *status);
float32 float16_to_float32(float16, flag, float_status *status);
float16 float64_to_float16(float64 a, flag ieee, float_status *status);
float64 float16_to_float64(float16 a, flag ieee, float_status *status);

/*----------------------------------------------------------------------------
| Software half-precision operations.
*----------------------------------------------------------------------------*/
int float16_is_quiet_nan(float16, float_status *status);
int float16_is_signaling_nan(float16, float_status *status);
float16 float16_maybe_silence_nan(float16, float_status *status);

static inline int float16_is_any_nan(float16 a)
{
    return ((float16_val(a) & ~0x8000) > 0x7c00);
}

static inline int float16_is_neg(float16 a)
{
    return float16_val(a) >> 15;
}

static inline int float16_is_infinity(float16 a)
{
    return (float16_val(a) & 0x7fff) == 0x7c00;
}

static inline int float16_is_zero(float16 a)
{
    return (float16_val(a) & 0x7fff) == 0;
}

static inline int float16_is_zero_or_denormal(float16 a)
{
    return (float16_val(a) & 0x7c00) == 0;
}

/*----------------------------------------------------------------------------
| The pattern for a default generated half-precision NaN.
*----------------------------------------------------------------------------*/
float16 float16_default_nan(float_status *status);

/*----------------------------------------------------------------------------
| Software IEC/IEEE single-precision conversion routines.
*----------------------------------------------------------------------------*/
int16_t float32_to_int16(float32, float_status *status);
uint16_t float32_to_uint16(float32, float_status *status);
int16_t float32_to_int16_round_to_zero(float32, float_status *status);
uint16_t float32_to_uint16_round_to_zero(float32, float_status *status);
int32_t float32_to_int32(float32, float_status *status);
int32_t float32_to_int32_round_to_zero(float32, float_status *status);
uint32_t float32_to_uint32(float32, float_status *status);
uint32_t float32_to_uint32_round_to_zero(float32, float_status *status);
int64_t float32_to_int64(float32, float_status *status);
uint64_t float32_to_uint64(float32, float_status *status);
uint64_t float32_to_uint64_round_to_zero(float32, float_status *status);
int64_t float32_to_int64_round_to_zero(float32, float_status *status);
float64 float32_to_float64(float32, float_status *status);
floatx80 float32_to_floatx80(float32, float_status *status);
float128 float32_to_float128(float32, float_status *status);

/*----------------------------------------------------------------------------
| Software IEC/IEEE single-precision operations.
*----------------------------------------------------------------------------*/
float32 float32_round_to_int(float32, float_status *status);
float32 float32_add(float32, float32, float_status *status);
float32 float32_sub(float32, float32, float_status *status);
float32 float32_mul(float32, float32, float_status *status);
float32 float32_div(float32, float32, float_status *status);
float32 float32_rem(float32, float32, float_status *status);
float32 float32_muladd(float32, float32, float32, int, float_status *status);
float32 float32_sqrt(float32, float_status *status);
float32 float32_exp2(float32, float_status *status);
float32 float32_log2(float32, float_status *status);
int float32_eq(float32, float32, float_status *status);
int float32_le(float32, float32, float_status *status);
int float32_lt(float32, float32, float_status *status);
int float32_unordered(float32, float32, float_status *status);
int float32_eq_quiet(float32, float32, float_status *status);
int float32_le_quiet(float32, float32, float_status *status);
int float32_lt_quiet(float32, float32, float_status *status);
int float32_unordered_quiet(float32, float32, float_status *status);
int float32_compare(float32, float32, float_status *status);
int float32_compare_quiet(float32, float32, float_status *status);
float32 float32_min(float32, float32, float_status *status);
float32 float32_max(float32, float32, float_status *status);
float32 float32_minnum(float32, float32, float_status *status);
float32 float32_maxnum(float32, float32, float_status *status);
float32 float32_minnummag(float32, float32, float_status *status);
float32 float32_maxnummag(float32, float32, float_status *status);
int float32_is_quiet_nan(float32, float_status *status);
int float32_is_signaling_nan(float32, float_status *status);
float32 float32_maybe_silence_nan(float32, float_status *status);
float32 float32_scalbn(float32, int, float_status *status);

static inline float32 float32_abs(float32 a)
{
    /* Note that abs does *not* handle NaN specially, nor does
     * it flush denormal inputs to zero.
     */
    return make_float32(float32_val(a) & 0x7fffffff);
}

static inline float32 float32_chs(float32 a)
{
    /* Note that chs does *not* handle NaN specially, nor does
     * it flush denormal inputs to zero.
     */
    return make_float32(float32_val(a) ^ 0x80000000);
}

static inline int float32_is_infinity(float32 a)
{
    return (float32_val(a) & 0x7fffffff) == 0x7f800000;
}

static inline int float32_is_neg(float32 a)
{
    return float32_val(a) >> 31;
}

static inline int float32_is_zero(float32 a)
{
    return (float32_val(a) & 0x7fffffff) == 0;
}

static inline int float32_is_any_nan(float32 a)
{
    return ((float32_val(a) & ~(1 << 31)) > 0x7f800000UL);
}

static inline int float32_is_zero_or_denormal(float32 a)
{
    return (float32_val(a) & 0x7f800000) == 0;
}

static inline float32 float32_set_sign(float32 a, int sign)
{
    return make_float32((float32_val(a) & 0x7fffffff) | (sign << 31));
}

#define float32_zero make_float32(0)
#define float32_one make_float32(0x3f800000)
#define float32_ln2 make_float32(0x3f317218)
#define float32_pi make_float32(0x40490fdb)
#define float32_half make_float32(0x3f000000)
#define float32_infinity make_float32(0x7f800000)


/*----------------------------------------------------------------------------
| The pattern for a default generated single-precision NaN.
*----------------------------------------------------------------------------*/
float32 float32_default_nan(float_status *status);

/*----------------------------------------------------------------------------
| Software IEC/IEEE double-precision conversion routines.
*----------------------------------------------------------------------------*/
int16_t float64_to_int16(float64, float_status *status);
uint16_t float64_to_uint16(float64, float_status *status);
int16_t float64_to_int16_round_to_zero(float64, float_status *status);
uint16_t float64_to_uint16_round_to_zero(float64, float_status *status);
int32_t float64_to_int32(float64, float_status *status);
int32_t float64_to_int32_round_to_zero(float64, float_status *status);
uint32_t float64_to_uint32(float64, float_status *status);
uint32_t float64_to_uint32_round_to_zero(float64, float_status *status);
int64_t float64_to_int64(float64, float_status *status);
int64_t float64_to_int64_round_to_zero(float64, float_status *status);
uint64_t float64_to_uint64(float64 a, float_status *status);
uint64_t float64_to_uint64_round_to_zero(float64 a, float_status *status);
float32 float64_to_float32(float64, float_status *status);
floatx80 float64_to_floatx80(float64, float_status *status);
float128 float64_to_float128(float64, float_status *status);

/*----------------------------------------------------------------------------
| Software IEC/IEEE double-precision operations.
*----------------------------------------------------------------------------*/
float64 float64_round_to_int(float64, float_status *status);
float64 float64_trunc_to_int(float64, float_status *status);
float64 float64_add(float64, float64, float_status *status);
float64 float64_sub(float64, float64, float_status *status);
float64 float64_mul(float64, float64, float_status *status);
float64 float64_div(float64, float64, float_status *status);
float64 float64_rem(float64, float64, float_status *status);
float64 float64_muladd(float64, float64, float64, int, float_status *status);
float64 float64_sqrt(float64, float_status *status);
float64 float64_log2(float64, float_status *status);
int float64_eq(float64, float64, float_status *status);
int float64_le(float64, float64, float_status *status);
int float64_lt(float64, float64, float_status *status);
int float64_unordered(float64, float64, float_status *status);
int float64_eq_quiet(float64, float64, float_status *status);
int float64_le_quiet(float64, float64, float_status *status);
int float64_lt_quiet(float64, float64, float_status *status);
int float64_unordered_quiet(float64, float64, float_status *status);
int float64_compare(float64, float64, float_status *status);
int float64_compare_quiet(float64, float64, float_status *status);
float64 float64_min(float64, float64, float_status *status);
float64 float64_max(float64, float64, float_status *status);
float64 float64_minnum(float64, float64, float_status *status);
float64 float64_maxnum(float64, float64, float_status *status);
float64 float64_minnummag(float64, float64, float_status *status);
float64 float64_maxnummag(float64, float64, float_status *status);
int float64_is_quiet_nan(float64 a, float_status *status);
int float64_is_signaling_nan(float64, float_status *status);
float64 float64_maybe_silence_nan(float64, float_status *status);
float64 float64_scalbn(float64, int, float_status *status);

static inline float64 float64_abs(float64 a)
{
    /* Note that abs does *not* handle NaN specially, nor does
     * it flush denormal inputs to zero.
     */
    return make_float64(float64_val(a) & 0x7fffffffffffffffLL);
}

static inline float64 float64_chs(float64 a)
{
    /* Note that chs does *not* handle NaN specially, nor does
     * it flush denormal inputs to zero.
     */
    return make_float64(float64_val(a) ^ 0x8000000000000000LL);
}

static inline int float64_is_infinity(float64 a)
{
    return (float64_val(a) & 0x7fffffffffffffffLL ) == 0x7ff0000000000000LL;
}

static inline int float64_is_neg(float64 a)
{
    return float64_val(a) >> 63;
}

static inline int float64_is_zero(float64 a)
{
    return (float64_val(a) & 0x7fffffffffffffffLL) == 0;
}

static inline int float64_is_any_nan(float64 a)
{
    return ((float64_val(a) & ~(1ULL << 63)) > 0x7ff0000000000000ULL);
}

static inline int float64_is_zero_or_denormal(float64 a)
{
    return (float64_val(a) & 0x7ff0000000000000LL) == 0;
}

static inline float64 float64_set_sign(float64 a, int sign)
{
    return make_float64((float64_val(a) & 0x7fffffffffffffffULL)
                        | ((int64_t)sign << 63));
}

#define float64_zero make_float64(0)
#define float64_one make_float64(0x3ff0000000000000LL)
#define float64_ln2 make_float64(0x3fe62e42fefa39efLL)
#define float64_pi make_float64(0x400921fb54442d18LL)
#define float64_half make_float64(0x3fe0000000000000LL)
#define float64_infinity make_float64(0x7ff0000000000000LL)

/*----------------------------------------------------------------------------
| The pattern for a default generated double-precision NaN.
*----------------------------------------------------------------------------*/
float64 float64_default_nan(float_status *status);

/*----------------------------------------------------------------------------
| Software IEC/IEEE extended double-precision conversion routines.
*----------------------------------------------------------------------------*/
int32_t floatx80_to_int32(floatx80, float_status *status);
int32_t floatx80_to_int32_round_to_zero(floatx80, float_status *status);
int64_t floatx80_to_int64(floatx80, float_status *status);
int64_t floatx80_to_int64_round_to_zero(floatx80, float_status *status);
float32 floatx80_to_float32(floatx80, float_status *status);
float64 floatx80_to_float64(floatx80, float_status *status);
float128 floatx80_to_float128(floatx80, float_status *status);

/*----------------------------------------------------------------------------
| Software IEC/IEEE extended double-precision operations.
*----------------------------------------------------------------------------*/
floatx80 floatx80_round_to_int(floatx80, float_status *status);
floatx80 floatx80_add(floatx80, floatx80, float_status *status);
floatx80 floatx80_sub(floatx80, floatx80, float_status *status);
floatx80 floatx80_mul(floatx80, floatx80, float_status *status);
floatx80 floatx80_div(floatx80, floatx80, float_status *status);
floatx80 floatx80_rem(floatx80, floatx80, float_status *status);
floatx80 floatx80_sqrt(floatx80, float_status *status);
int floatx80_eq(floatx80, floatx80, float_status *status);
int floatx80_le(floatx80, floatx80, float_status *status);
int floatx80_lt(floatx80, floatx80, float_status *status);
int floatx80_unordered(floatx80, floatx80, float_status *status);
int floatx80_eq_quiet(floatx80, floatx80, float_status *status);
int floatx80_le_quiet(floatx80, floatx80, float_status *status);
int floatx80_lt_quiet(floatx80, floatx80, float_status *status);
int floatx80_unordered_quiet(floatx80, floatx80, float_status *status);
int floatx80_compare(floatx80, floatx80, float_status *status);
int floatx80_compare_quiet(floatx80, floatx80, float_status *status);
int floatx80_is_quiet_nan(floatx80, float_status *status);
int floatx80_is_signaling_nan(floatx80, float_status *status);
floatx80 floatx80_maybe_silence_nan(floatx80, float_status *status);
floatx80 floatx80_scalbn(floatx80, int, float_status *status);

static inline floatx80 floatx80_abs(floatx80 a)
{
    a.high &= 0x7fff;
    return a;
}

static inline floatx80 floatx80_chs(floatx80 a)
{
    a.high ^= 0x8000;
    return a;
}

static inline int floatx80_is_infinity(floatx80 a)
{
    return (a.high & 0x7fff) == 0x7fff && a.low == 0x8000000000000000LL;
}

static inline int floatx80_is_neg(floatx80 a)
{
    return a.high >> 15;
}

static inline int floatx80_is_zero(floatx80 a)
{
    return (a.high & 0x7fff) == 0 && a.low == 0;
}

static inline int floatx80_is_zero_or_denormal(floatx80 a)
{
    return (a.high & 0x7fff) == 0;
}

static inline int floatx80_is_any_nan(floatx80 a)
{
    return ((a.high & 0x7fff) == 0x7fff) && (a.low<<1);
}

/*----------------------------------------------------------------------------
| Return whether the given value is an invalid floatx80 encoding.
| Invalid floatx80 encodings arise when the integer bit is not set, but
| the exponent is not zero. The only times the integer bit is permitted to
| be zero is in subnormal numbers and the value zero.
| This includes what the Intel software developer's manual calls pseudo-NaNs,
| pseudo-infinities and un-normal numbers. It does not include
| pseudo-denormals, which must still be correctly handled as inputs even
| if they are never generated as outputs.
*----------------------------------------------------------------------------*/
static inline bool floatx80_invalid_encoding(floatx80 a)
{
    return (a.low & (1ULL << 63)) == 0 && (a.high & 0x7FFF) != 0;
}

#define floatx80_zero make_floatx80(0x0000, 0x0000000000000000LL)
#define floatx80_one make_floatx80(0x3fff, 0x8000000000000000LL)
#define floatx80_ln2 make_floatx80(0x3ffe, 0xb17217f7d1cf79acLL)
#define floatx80_pi make_floatx80(0x4000, 0xc90fdaa22168c235LL)
#define floatx80_half make_floatx80(0x3ffe, 0x8000000000000000LL)
#define floatx80_infinity make_floatx80(0x7fff, 0x8000000000000000LL)

/*----------------------------------------------------------------------------
| The pattern for a default generated extended double-precision NaN.
*----------------------------------------------------------------------------*/
floatx80 floatx80_default_nan(float_status *status);

/*----------------------------------------------------------------------------
| Software IEC/IEEE quadruple-precision conversion routines.
*----------------------------------------------------------------------------*/
int32_t float128_to_int32(float128, float_status *status);
int32_t float128_to_int32_round_to_zero(float128, float_status *status);
int64_t float128_to_int64(float128, float_status *status);
int64_t float128_to_int64_round_to_zero(float128, float_status *status);
uint64_t float128_to_uint64(float128, float_status *status);
uint64_t float128_to_uint64_round_to_zero(float128, float_status *status);
uint32_t float128_to_uint32_round_to_zero(float128, float_status *status);
float32 float128_to_float32(float128, float_status *status);
float64 float128_to_float64(float128, float_status *status);
floatx80 float128_to_floatx80(float128, float_status *status);

/*----------------------------------------------------------------------------
| Software IEC/IEEE quadruple-precision operations.
*----------------------------------------------------------------------------*/
float128 float128_round_to_int(float128, float_status *status);
float128 float128_add(float128, float128, float_status *status);
float128 float128_sub(float128, float128, float_status *status);
float128 float128_mul(float128, float128, float_status *status);
float128 float128_div(float128, float128, float_status *status);
float128 float128_rem(float128, float128, float_status *status);
float128 float128_sqrt(float128, float_status *status);
int float128_eq(float128, float128, float_status *status);
int float128_le(float128, float128, float_status *status);
int float128_lt(float128, float128, float_status *status);
int float128_unordered(float128, float128, float_status *status);
int float128_eq_quiet(float128, float128, float_status *status);
int float128_le_quiet(float128, float128, float_status *status);
int float128_lt_quiet(float128, float128, float_status *status);
int float128_unordered_quiet(float128, float128, float_status *status);
int float128_compare(float128, float128, float_status *status);
int float128_compare_quiet(float128, float128, float_status *status);
int float128_is_quiet_nan(float128, float_status *status);
int float128_is_signaling_nan(float128, float_status *status);
float128 float128_maybe_silence_nan(float128, float_status *status);
float128 float128_scalbn(float128, int, float_status *status);

static inline float128 float128_abs(float128 a)
{
    a.high &= 0x7fffffffffffffffLL;
    return a;
}

static inline float128 float128_chs(float128 a)
{
    a.high ^= 0x8000000000000000LL;
    return a;
}

static inline int float128_is_infinity(float128 a)
{
    return (a.high & 0x7fffffffffffffffLL) == 0x7fff000000000000LL && a.low == 0;
}

static inline int float128_is_neg(float128 a)
{
    return a.high >> 63;
}

static inline int float128_is_zero(float128 a)
{
    return (a.high & 0x7fffffffffffffffLL) == 0 && a.low == 0;
}

static inline int float128_is_zero_or_denormal(float128 a)
{
    return (a.high & 0x7fff000000000000LL) == 0;
}

static inline int float128_is_any_nan(float128 a)
{
    return ((a.high >> 48) & 0x7fff) == 0x7fff &&
        ((a.low != 0) || ((a.high & 0xffffffffffffLL) != 0));
}

#define float128_zero make_float128(0, 0)

/*----------------------------------------------------------------------------
| The pattern for a default generated quadruple-precision NaN.
*----------------------------------------------------------------------------*/
float128 float128_default_nan(float_status *status);

#endif /* SOFTFLOAT_H */
