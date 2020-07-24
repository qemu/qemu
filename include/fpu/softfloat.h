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

/*----------------------------------------------------------------------------
| Software IEC/IEEE floating-point ordering relations
*----------------------------------------------------------------------------*/

typedef enum {
    float_relation_less      = -1,
    float_relation_equal     =  0,
    float_relation_greater   =  1,
    float_relation_unordered =  2
} FloatRelation;

#include "fpu/softfloat-types.h"
#include "fpu/softfloat-helpers.h"

/*----------------------------------------------------------------------------
| Routine to raise any or all of the software IEC/IEEE floating-point
| exception flags.
*----------------------------------------------------------------------------*/
void float_raise(uint8_t flags, float_status *status);

/*----------------------------------------------------------------------------
| If `a' is denormal and we are in flush-to-zero mode then set the
| input-denormal exception and return zero. Otherwise just return the value.
*----------------------------------------------------------------------------*/
float16 float16_squash_input_denormal(float16 a, float_status *status);
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

float16 int16_to_float16_scalbn(int16_t a, int, float_status *status);
float16 int32_to_float16_scalbn(int32_t a, int, float_status *status);
float16 int64_to_float16_scalbn(int64_t a, int, float_status *status);
float16 uint16_to_float16_scalbn(uint16_t a, int, float_status *status);
float16 uint32_to_float16_scalbn(uint32_t a, int, float_status *status);
float16 uint64_to_float16_scalbn(uint64_t a, int, float_status *status);

float16 int16_to_float16(int16_t a, float_status *status);
float16 int32_to_float16(int32_t a, float_status *status);
float16 int64_to_float16(int64_t a, float_status *status);
float16 uint16_to_float16(uint16_t a, float_status *status);
float16 uint32_to_float16(uint32_t a, float_status *status);
float16 uint64_to_float16(uint64_t a, float_status *status);

float32 int16_to_float32_scalbn(int16_t, int, float_status *status);
float32 int32_to_float32_scalbn(int32_t, int, float_status *status);
float32 int64_to_float32_scalbn(int64_t, int, float_status *status);
float32 uint16_to_float32_scalbn(uint16_t, int, float_status *status);
float32 uint32_to_float32_scalbn(uint32_t, int, float_status *status);
float32 uint64_to_float32_scalbn(uint64_t, int, float_status *status);

float32 int16_to_float32(int16_t, float_status *status);
float32 int32_to_float32(int32_t, float_status *status);
float32 int64_to_float32(int64_t, float_status *status);
float32 uint16_to_float32(uint16_t, float_status *status);
float32 uint32_to_float32(uint32_t, float_status *status);
float32 uint64_to_float32(uint64_t, float_status *status);

float64 int16_to_float64_scalbn(int16_t, int, float_status *status);
float64 int32_to_float64_scalbn(int32_t, int, float_status *status);
float64 int64_to_float64_scalbn(int64_t, int, float_status *status);
float64 uint16_to_float64_scalbn(uint16_t, int, float_status *status);
float64 uint32_to_float64_scalbn(uint32_t, int, float_status *status);
float64 uint64_to_float64_scalbn(uint64_t, int, float_status *status);

float64 int16_to_float64(int16_t, float_status *status);
float64 int32_to_float64(int32_t, float_status *status);
float64 int64_to_float64(int64_t, float_status *status);
float64 uint16_to_float64(uint16_t, float_status *status);
float64 uint32_to_float64(uint32_t, float_status *status);
float64 uint64_to_float64(uint64_t, float_status *status);

floatx80 int32_to_floatx80(int32_t, float_status *status);
floatx80 int64_to_floatx80(int64_t, float_status *status);

float128 int32_to_float128(int32_t, float_status *status);
float128 int64_to_float128(int64_t, float_status *status);
float128 uint64_to_float128(uint64_t, float_status *status);

/*----------------------------------------------------------------------------
| Software half-precision conversion routines.
*----------------------------------------------------------------------------*/

float16 float32_to_float16(float32, bool ieee, float_status *status);
float32 float16_to_float32(float16, bool ieee, float_status *status);
float16 float64_to_float16(float64 a, bool ieee, float_status *status);
float64 float16_to_float64(float16 a, bool ieee, float_status *status);

int16_t float16_to_int16_scalbn(float16, FloatRoundMode, int, float_status *);
int32_t float16_to_int32_scalbn(float16, FloatRoundMode, int, float_status *);
int64_t float16_to_int64_scalbn(float16, FloatRoundMode, int, float_status *);

int16_t float16_to_int16(float16, float_status *status);
int32_t float16_to_int32(float16, float_status *status);
int64_t float16_to_int64(float16, float_status *status);

int16_t float16_to_int16_round_to_zero(float16, float_status *status);
int32_t float16_to_int32_round_to_zero(float16, float_status *status);
int64_t float16_to_int64_round_to_zero(float16, float_status *status);

uint16_t float16_to_uint16_scalbn(float16 a, FloatRoundMode,
                                  int, float_status *status);
uint32_t float16_to_uint32_scalbn(float16 a, FloatRoundMode,
                                  int, float_status *status);
uint64_t float16_to_uint64_scalbn(float16 a, FloatRoundMode,
                                  int, float_status *status);

uint16_t float16_to_uint16(float16 a, float_status *status);
uint32_t float16_to_uint32(float16 a, float_status *status);
uint64_t float16_to_uint64(float16 a, float_status *status);

uint16_t float16_to_uint16_round_to_zero(float16 a, float_status *status);
uint32_t float16_to_uint32_round_to_zero(float16 a, float_status *status);
uint64_t float16_to_uint64_round_to_zero(float16 a, float_status *status);

/*----------------------------------------------------------------------------
| Software half-precision operations.
*----------------------------------------------------------------------------*/

float16 float16_round_to_int(float16, float_status *status);
float16 float16_add(float16, float16, float_status *status);
float16 float16_sub(float16, float16, float_status *status);
float16 float16_mul(float16, float16, float_status *status);
float16 float16_muladd(float16, float16, float16, int, float_status *status);
float16 float16_div(float16, float16, float_status *status);
float16 float16_scalbn(float16, int, float_status *status);
float16 float16_min(float16, float16, float_status *status);
float16 float16_max(float16, float16, float_status *status);
float16 float16_minnum(float16, float16, float_status *status);
float16 float16_maxnum(float16, float16, float_status *status);
float16 float16_minnummag(float16, float16, float_status *status);
float16 float16_maxnummag(float16, float16, float_status *status);
float16 float16_sqrt(float16, float_status *status);
FloatRelation float16_compare(float16, float16, float_status *status);
FloatRelation float16_compare_quiet(float16, float16, float_status *status);

bool float16_is_quiet_nan(float16, float_status *status);
bool float16_is_signaling_nan(float16, float_status *status);
float16 float16_silence_nan(float16, float_status *status);

static inline bool float16_is_any_nan(float16 a)
{
    return ((float16_val(a) & ~0x8000) > 0x7c00);
}

static inline bool float16_is_neg(float16 a)
{
    return float16_val(a) >> 15;
}

static inline bool float16_is_infinity(float16 a)
{
    return (float16_val(a) & 0x7fff) == 0x7c00;
}

static inline bool float16_is_zero(float16 a)
{
    return (float16_val(a) & 0x7fff) == 0;
}

static inline bool float16_is_zero_or_denormal(float16 a)
{
    return (float16_val(a) & 0x7c00) == 0;
}

static inline float16 float16_abs(float16 a)
{
    /* Note that abs does *not* handle NaN specially, nor does
     * it flush denormal inputs to zero.
     */
    return make_float16(float16_val(a) & 0x7fff);
}

static inline float16 float16_chs(float16 a)
{
    /* Note that chs does *not* handle NaN specially, nor does
     * it flush denormal inputs to zero.
     */
    return make_float16(float16_val(a) ^ 0x8000);
}

static inline float16 float16_set_sign(float16 a, int sign)
{
    return make_float16((float16_val(a) & 0x7fff) | (sign << 15));
}

#define float16_zero make_float16(0)
#define float16_half make_float16(0x3800)
#define float16_one make_float16(0x3c00)
#define float16_one_point_five make_float16(0x3e00)
#define float16_two make_float16(0x4000)
#define float16_three make_float16(0x4200)
#define float16_infinity make_float16(0x7c00)

/*----------------------------------------------------------------------------
| The pattern for a default generated half-precision NaN.
*----------------------------------------------------------------------------*/
float16 float16_default_nan(float_status *status);

/*----------------------------------------------------------------------------
| Software IEC/IEEE single-precision conversion routines.
*----------------------------------------------------------------------------*/

int16_t float32_to_int16_scalbn(float32, FloatRoundMode, int, float_status *);
int32_t float32_to_int32_scalbn(float32, FloatRoundMode, int, float_status *);
int64_t float32_to_int64_scalbn(float32, FloatRoundMode, int, float_status *);

int16_t float32_to_int16(float32, float_status *status);
int32_t float32_to_int32(float32, float_status *status);
int64_t float32_to_int64(float32, float_status *status);

int16_t float32_to_int16_round_to_zero(float32, float_status *status);
int32_t float32_to_int32_round_to_zero(float32, float_status *status);
int64_t float32_to_int64_round_to_zero(float32, float_status *status);

uint16_t float32_to_uint16_scalbn(float32, FloatRoundMode, int, float_status *);
uint32_t float32_to_uint32_scalbn(float32, FloatRoundMode, int, float_status *);
uint64_t float32_to_uint64_scalbn(float32, FloatRoundMode, int, float_status *);

uint16_t float32_to_uint16(float32, float_status *status);
uint32_t float32_to_uint32(float32, float_status *status);
uint64_t float32_to_uint64(float32, float_status *status);

uint16_t float32_to_uint16_round_to_zero(float32, float_status *status);
uint32_t float32_to_uint32_round_to_zero(float32, float_status *status);
uint64_t float32_to_uint64_round_to_zero(float32, float_status *status);

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
FloatRelation float32_compare(float32, float32, float_status *status);
FloatRelation float32_compare_quiet(float32, float32, float_status *status);
float32 float32_min(float32, float32, float_status *status);
float32 float32_max(float32, float32, float_status *status);
float32 float32_minnum(float32, float32, float_status *status);
float32 float32_maxnum(float32, float32, float_status *status);
float32 float32_minnummag(float32, float32, float_status *status);
float32 float32_maxnummag(float32, float32, float_status *status);
bool float32_is_quiet_nan(float32, float_status *status);
bool float32_is_signaling_nan(float32, float_status *status);
float32 float32_silence_nan(float32, float_status *status);
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

static inline bool float32_is_infinity(float32 a)
{
    return (float32_val(a) & 0x7fffffff) == 0x7f800000;
}

static inline bool float32_is_neg(float32 a)
{
    return float32_val(a) >> 31;
}

static inline bool float32_is_zero(float32 a)
{
    return (float32_val(a) & 0x7fffffff) == 0;
}

static inline bool float32_is_any_nan(float32 a)
{
    return ((float32_val(a) & ~(1 << 31)) > 0x7f800000UL);
}

static inline bool float32_is_zero_or_denormal(float32 a)
{
    return (float32_val(a) & 0x7f800000) == 0;
}

static inline bool float32_is_normal(float32 a)
{
    return (((float32_val(a) >> 23) + 1) & 0xff) >= 2;
}

static inline bool float32_is_denormal(float32 a)
{
    return float32_is_zero_or_denormal(a) && !float32_is_zero(a);
}

static inline bool float32_is_zero_or_normal(float32 a)
{
    return float32_is_normal(a) || float32_is_zero(a);
}

static inline float32 float32_set_sign(float32 a, int sign)
{
    return make_float32((float32_val(a) & 0x7fffffff) | (sign << 31));
}

static inline bool float32_eq(float32 a, float32 b, float_status *s)
{
    return float32_compare(a, b, s) == float_relation_equal;
}

static inline bool float32_le(float32 a, float32 b, float_status *s)
{
    return float32_compare(a, b, s) <= float_relation_equal;
}

static inline bool float32_lt(float32 a, float32 b, float_status *s)
{
    return float32_compare(a, b, s) < float_relation_equal;
}

static inline bool float32_unordered(float32 a, float32 b, float_status *s)
{
    return float32_compare(a, b, s) == float_relation_unordered;
}

static inline bool float32_eq_quiet(float32 a, float32 b, float_status *s)
{
    return float32_compare_quiet(a, b, s) == float_relation_equal;
}

static inline bool float32_le_quiet(float32 a, float32 b, float_status *s)
{
    return float32_compare_quiet(a, b, s) <= float_relation_equal;
}

static inline bool float32_lt_quiet(float32 a, float32 b, float_status *s)
{
    return float32_compare_quiet(a, b, s) < float_relation_equal;
}

static inline bool float32_unordered_quiet(float32 a, float32 b,
                                           float_status *s)
{
    return float32_compare_quiet(a, b, s) == float_relation_unordered;
}

#define float32_zero make_float32(0)
#define float32_half make_float32(0x3f000000)
#define float32_one make_float32(0x3f800000)
#define float32_one_point_five make_float32(0x3fc00000)
#define float32_two make_float32(0x40000000)
#define float32_three make_float32(0x40400000)
#define float32_infinity make_float32(0x7f800000)

/*----------------------------------------------------------------------------
| Packs the sign `zSign', exponent `zExp', and significand `zSig' into a
| single-precision floating-point value, returning the result.  After being
| shifted into the proper positions, the three fields are simply added
| together to form the result.  This means that any integer portion of `zSig'
| will be added into the exponent.  Since a properly normalized significand
| will have an integer portion equal to 1, the `zExp' input should be 1 less
| than the desired result exponent whenever `zSig' is a complete, normalized
| significand.
*----------------------------------------------------------------------------*/

static inline float32 packFloat32(bool zSign, int zExp, uint32_t zSig)
{
    return make_float32(
          (((uint32_t)zSign) << 31) + (((uint32_t)zExp) << 23) + zSig);
}

/*----------------------------------------------------------------------------
| The pattern for a default generated single-precision NaN.
*----------------------------------------------------------------------------*/
float32 float32_default_nan(float_status *status);

/*----------------------------------------------------------------------------
| Software IEC/IEEE double-precision conversion routines.
*----------------------------------------------------------------------------*/

int16_t float64_to_int16_scalbn(float64, FloatRoundMode, int, float_status *);
int32_t float64_to_int32_scalbn(float64, FloatRoundMode, int, float_status *);
int64_t float64_to_int64_scalbn(float64, FloatRoundMode, int, float_status *);

int16_t float64_to_int16(float64, float_status *status);
int32_t float64_to_int32(float64, float_status *status);
int64_t float64_to_int64(float64, float_status *status);

int16_t float64_to_int16_round_to_zero(float64, float_status *status);
int32_t float64_to_int32_round_to_zero(float64, float_status *status);
int64_t float64_to_int64_round_to_zero(float64, float_status *status);

uint16_t float64_to_uint16_scalbn(float64, FloatRoundMode, int, float_status *);
uint32_t float64_to_uint32_scalbn(float64, FloatRoundMode, int, float_status *);
uint64_t float64_to_uint64_scalbn(float64, FloatRoundMode, int, float_status *);

uint16_t float64_to_uint16(float64, float_status *status);
uint32_t float64_to_uint32(float64, float_status *status);
uint64_t float64_to_uint64(float64, float_status *status);

uint16_t float64_to_uint16_round_to_zero(float64, float_status *status);
uint32_t float64_to_uint32_round_to_zero(float64, float_status *status);
uint64_t float64_to_uint64_round_to_zero(float64, float_status *status);

float32 float64_to_float32(float64, float_status *status);
floatx80 float64_to_floatx80(float64, float_status *status);
float128 float64_to_float128(float64, float_status *status);

/*----------------------------------------------------------------------------
| Software IEC/IEEE double-precision operations.
*----------------------------------------------------------------------------*/
float64 float64_round_to_int(float64, float_status *status);
float64 float64_add(float64, float64, float_status *status);
float64 float64_sub(float64, float64, float_status *status);
float64 float64_mul(float64, float64, float_status *status);
float64 float64_div(float64, float64, float_status *status);
float64 float64_rem(float64, float64, float_status *status);
float64 float64_muladd(float64, float64, float64, int, float_status *status);
float64 float64_sqrt(float64, float_status *status);
float64 float64_log2(float64, float_status *status);
FloatRelation float64_compare(float64, float64, float_status *status);
FloatRelation float64_compare_quiet(float64, float64, float_status *status);
float64 float64_min(float64, float64, float_status *status);
float64 float64_max(float64, float64, float_status *status);
float64 float64_minnum(float64, float64, float_status *status);
float64 float64_maxnum(float64, float64, float_status *status);
float64 float64_minnummag(float64, float64, float_status *status);
float64 float64_maxnummag(float64, float64, float_status *status);
bool float64_is_quiet_nan(float64 a, float_status *status);
bool float64_is_signaling_nan(float64, float_status *status);
float64 float64_silence_nan(float64, float_status *status);
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

static inline bool float64_is_infinity(float64 a)
{
    return (float64_val(a) & 0x7fffffffffffffffLL ) == 0x7ff0000000000000LL;
}

static inline bool float64_is_neg(float64 a)
{
    return float64_val(a) >> 63;
}

static inline bool float64_is_zero(float64 a)
{
    return (float64_val(a) & 0x7fffffffffffffffLL) == 0;
}

static inline bool float64_is_any_nan(float64 a)
{
    return ((float64_val(a) & ~(1ULL << 63)) > 0x7ff0000000000000ULL);
}

static inline bool float64_is_zero_or_denormal(float64 a)
{
    return (float64_val(a) & 0x7ff0000000000000LL) == 0;
}

static inline bool float64_is_normal(float64 a)
{
    return (((float64_val(a) >> 52) + 1) & 0x7ff) >= 2;
}

static inline bool float64_is_denormal(float64 a)
{
    return float64_is_zero_or_denormal(a) && !float64_is_zero(a);
}

static inline bool float64_is_zero_or_normal(float64 a)
{
    return float64_is_normal(a) || float64_is_zero(a);
}

static inline float64 float64_set_sign(float64 a, int sign)
{
    return make_float64((float64_val(a) & 0x7fffffffffffffffULL)
                        | ((int64_t)sign << 63));
}

static inline bool float64_eq(float64 a, float64 b, float_status *s)
{
    return float64_compare(a, b, s) == float_relation_equal;
}

static inline bool float64_le(float64 a, float64 b, float_status *s)
{
    return float64_compare(a, b, s) <= float_relation_equal;
}

static inline bool float64_lt(float64 a, float64 b, float_status *s)
{
    return float64_compare(a, b, s) < float_relation_equal;
}

static inline bool float64_unordered(float64 a, float64 b, float_status *s)
{
    return float64_compare(a, b, s) == float_relation_unordered;
}

static inline bool float64_eq_quiet(float64 a, float64 b, float_status *s)
{
    return float64_compare_quiet(a, b, s) == float_relation_equal;
}

static inline bool float64_le_quiet(float64 a, float64 b, float_status *s)
{
    return float64_compare_quiet(a, b, s) <= float_relation_equal;
}

static inline bool float64_lt_quiet(float64 a, float64 b, float_status *s)
{
    return float64_compare_quiet(a, b, s) < float_relation_equal;
}

static inline bool float64_unordered_quiet(float64 a, float64 b,
                                           float_status *s)
{
    return float64_compare_quiet(a, b, s) == float_relation_unordered;
}

#define float64_zero make_float64(0)
#define float64_half make_float64(0x3fe0000000000000LL)
#define float64_one make_float64(0x3ff0000000000000LL)
#define float64_one_point_five make_float64(0x3FF8000000000000ULL)
#define float64_two make_float64(0x4000000000000000ULL)
#define float64_three make_float64(0x4008000000000000ULL)
#define float64_ln2 make_float64(0x3fe62e42fefa39efLL)
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
| The pattern for an extended double-precision inf.
*----------------------------------------------------------------------------*/
extern const floatx80 floatx80_infinity;

/*----------------------------------------------------------------------------
| Software IEC/IEEE extended double-precision operations.
*----------------------------------------------------------------------------*/
floatx80 floatx80_round(floatx80 a, float_status *status);
floatx80 floatx80_round_to_int(floatx80, float_status *status);
floatx80 floatx80_add(floatx80, floatx80, float_status *status);
floatx80 floatx80_sub(floatx80, floatx80, float_status *status);
floatx80 floatx80_mul(floatx80, floatx80, float_status *status);
floatx80 floatx80_div(floatx80, floatx80, float_status *status);
floatx80 floatx80_modrem(floatx80, floatx80, bool, uint64_t *,
                         float_status *status);
floatx80 floatx80_mod(floatx80, floatx80, float_status *status);
floatx80 floatx80_rem(floatx80, floatx80, float_status *status);
floatx80 floatx80_sqrt(floatx80, float_status *status);
FloatRelation floatx80_compare(floatx80, floatx80, float_status *status);
FloatRelation floatx80_compare_quiet(floatx80, floatx80, float_status *status);
int floatx80_is_quiet_nan(floatx80, float_status *status);
int floatx80_is_signaling_nan(floatx80, float_status *status);
floatx80 floatx80_silence_nan(floatx80, float_status *status);
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

static inline bool floatx80_is_infinity(floatx80 a)
{
#if defined(TARGET_M68K)
    return (a.high & 0x7fff) == floatx80_infinity.high && !(a.low << 1);
#else
    return (a.high & 0x7fff) == floatx80_infinity.high &&
                       a.low == floatx80_infinity.low;
#endif
}

static inline bool floatx80_is_neg(floatx80 a)
{
    return a.high >> 15;
}

static inline bool floatx80_is_zero(floatx80 a)
{
    return (a.high & 0x7fff) == 0 && a.low == 0;
}

static inline bool floatx80_is_zero_or_denormal(floatx80 a)
{
    return (a.high & 0x7fff) == 0;
}

static inline bool floatx80_is_any_nan(floatx80 a)
{
    return ((a.high & 0x7fff) == 0x7fff) && (a.low<<1);
}

static inline bool floatx80_eq(floatx80 a, floatx80 b, float_status *s)
{
    return floatx80_compare(a, b, s) == float_relation_equal;
}

static inline bool floatx80_le(floatx80 a, floatx80 b, float_status *s)
{
    return floatx80_compare(a, b, s) <= float_relation_equal;
}

static inline bool floatx80_lt(floatx80 a, floatx80 b, float_status *s)
{
    return floatx80_compare(a, b, s) < float_relation_equal;
}

static inline bool floatx80_unordered(floatx80 a, floatx80 b, float_status *s)
{
    return floatx80_compare(a, b, s) == float_relation_unordered;
}

static inline bool floatx80_eq_quiet(floatx80 a, floatx80 b, float_status *s)
{
    return floatx80_compare_quiet(a, b, s) == float_relation_equal;
}

static inline bool floatx80_le_quiet(floatx80 a, floatx80 b, float_status *s)
{
    return floatx80_compare_quiet(a, b, s) <= float_relation_equal;
}

static inline bool floatx80_lt_quiet(floatx80 a, floatx80 b, float_status *s)
{
    return floatx80_compare_quiet(a, b, s) < float_relation_equal;
}

static inline bool floatx80_unordered_quiet(floatx80 a, floatx80 b,
                                           float_status *s)
{
    return floatx80_compare_quiet(a, b, s) == float_relation_unordered;
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
#if defined(TARGET_M68K)
    /*-------------------------------------------------------------------------
    | With m68k, the explicit integer bit can be zero in the case of:
    | - zeros                (exp == 0, mantissa == 0)
    | - denormalized numbers (exp == 0, mantissa != 0)
    | - unnormalized numbers (exp != 0, exp < 0x7FFF)
    | - infinities           (exp == 0x7FFF, mantissa == 0)
    | - not-a-numbers        (exp == 0x7FFF, mantissa != 0)
    |
    | For infinities and NaNs, the explicit integer bit can be either one or
    | zero.
    |
    | The IEEE 754 standard does not define a zero integer bit. Such a number
    | is an unnormalized number. Hardware does not directly support
    | denormalized and unnormalized numbers, but implicitly supports them by
    | trapping them as unimplemented data types, allowing efficient conversion
    | in software.
    |
    | See "M68000 FAMILY PROGRAMMER’S REFERENCE MANUAL",
    |     "1.6 FLOATING-POINT DATA TYPES"
    *------------------------------------------------------------------------*/
    return false;
#else
    return (a.low & (1ULL << 63)) == 0 && (a.high & 0x7FFF) != 0;
#endif
}

#define floatx80_zero make_floatx80(0x0000, 0x0000000000000000LL)
#define floatx80_zero_init make_floatx80_init(0x0000, 0x0000000000000000LL)
#define floatx80_one make_floatx80(0x3fff, 0x8000000000000000LL)
#define floatx80_ln2 make_floatx80(0x3ffe, 0xb17217f7d1cf79acLL)
#define floatx80_pi make_floatx80(0x4000, 0xc90fdaa22168c235LL)
#define floatx80_half make_floatx80(0x3ffe, 0x8000000000000000LL)

/*----------------------------------------------------------------------------
| Returns the fraction bits of the extended double-precision floating-point
| value `a'.
*----------------------------------------------------------------------------*/

static inline uint64_t extractFloatx80Frac(floatx80 a)
{
    return a.low;
}

/*----------------------------------------------------------------------------
| Returns the exponent bits of the extended double-precision floating-point
| value `a'.
*----------------------------------------------------------------------------*/

static inline int32_t extractFloatx80Exp(floatx80 a)
{
    return a.high & 0x7FFF;
}

/*----------------------------------------------------------------------------
| Returns the sign bit of the extended double-precision floating-point value
| `a'.
*----------------------------------------------------------------------------*/

static inline bool extractFloatx80Sign(floatx80 a)
{
    return a.high >> 15;
}

/*----------------------------------------------------------------------------
| Packs the sign `zSign', exponent `zExp', and significand `zSig' into an
| extended double-precision floating-point value, returning the result.
*----------------------------------------------------------------------------*/

static inline floatx80 packFloatx80(bool zSign, int32_t zExp, uint64_t zSig)
{
    floatx80 z;

    z.low = zSig;
    z.high = (((uint16_t)zSign) << 15) + zExp;
    return z;
}

/*----------------------------------------------------------------------------
| Normalizes the subnormal extended double-precision floating-point value
| represented by the denormalized significand `aSig'.  The normalized exponent
| and significand are stored at the locations pointed to by `zExpPtr' and
| `zSigPtr', respectively.
*----------------------------------------------------------------------------*/

void normalizeFloatx80Subnormal(uint64_t aSig, int32_t *zExpPtr,
                                uint64_t *zSigPtr);

/*----------------------------------------------------------------------------
| Takes two extended double-precision floating-point values `a' and `b', one
| of which is a NaN, and returns the appropriate NaN result.  If either `a' or
| `b' is a signaling NaN, the invalid exception is raised.
*----------------------------------------------------------------------------*/

floatx80 propagateFloatx80NaN(floatx80 a, floatx80 b, float_status *status);

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

floatx80 roundAndPackFloatx80(int8_t roundingPrecision, bool zSign,
                              int32_t zExp, uint64_t zSig0, uint64_t zSig1,
                              float_status *status);

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent
| `zExp', and significand formed by the concatenation of `zSig0' and `zSig1',
| and returns the proper extended double-precision floating-point value
| corresponding to the abstract input.  This routine is just like
| `roundAndPackFloatx80' except that the input significand does not have to be
| normalized.
*----------------------------------------------------------------------------*/

floatx80 normalizeRoundAndPackFloatx80(int8_t roundingPrecision,
                                       bool zSign, int32_t zExp,
                                       uint64_t zSig0, uint64_t zSig1,
                                       float_status *status);

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
uint32_t float128_to_uint32(float128, float_status *status);
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
FloatRelation float128_compare(float128, float128, float_status *status);
FloatRelation float128_compare_quiet(float128, float128, float_status *status);
bool float128_is_quiet_nan(float128, float_status *status);
bool float128_is_signaling_nan(float128, float_status *status);
float128 float128_silence_nan(float128, float_status *status);
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

static inline bool float128_is_infinity(float128 a)
{
    return (a.high & 0x7fffffffffffffffLL) == 0x7fff000000000000LL && a.low == 0;
}

static inline bool float128_is_neg(float128 a)
{
    return a.high >> 63;
}

static inline bool float128_is_zero(float128 a)
{
    return (a.high & 0x7fffffffffffffffLL) == 0 && a.low == 0;
}

static inline bool float128_is_zero_or_denormal(float128 a)
{
    return (a.high & 0x7fff000000000000LL) == 0;
}

static inline bool float128_is_normal(float128 a)
{
    return (((a.high >> 48) + 1) & 0x7fff) >= 2;
}

static inline bool float128_is_denormal(float128 a)
{
    return float128_is_zero_or_denormal(a) && !float128_is_zero(a);
}

static inline bool float128_is_any_nan(float128 a)
{
    return ((a.high >> 48) & 0x7fff) == 0x7fff &&
        ((a.low != 0) || ((a.high & 0xffffffffffffLL) != 0));
}

static inline bool float128_eq(float128 a, float128 b, float_status *s)
{
    return float128_compare(a, b, s) == float_relation_equal;
}

static inline bool float128_le(float128 a, float128 b, float_status *s)
{
    return float128_compare(a, b, s) <= float_relation_equal;
}

static inline bool float128_lt(float128 a, float128 b, float_status *s)
{
    return float128_compare(a, b, s) < float_relation_equal;
}

static inline bool float128_unordered(float128 a, float128 b, float_status *s)
{
    return float128_compare(a, b, s) == float_relation_unordered;
}

static inline bool float128_eq_quiet(float128 a, float128 b, float_status *s)
{
    return float128_compare_quiet(a, b, s) == float_relation_equal;
}

static inline bool float128_le_quiet(float128 a, float128 b, float_status *s)
{
    return float128_compare_quiet(a, b, s) <= float_relation_equal;
}

static inline bool float128_lt_quiet(float128 a, float128 b, float_status *s)
{
    return float128_compare_quiet(a, b, s) < float_relation_equal;
}

static inline bool float128_unordered_quiet(float128 a, float128 b,
                                           float_status *s)
{
    return float128_compare_quiet(a, b, s) == float_relation_unordered;
}

#define float128_zero make_float128(0, 0)

/*----------------------------------------------------------------------------
| The pattern for a default generated quadruple-precision NaN.
*----------------------------------------------------------------------------*/
float128 float128_default_nan(float_status *status);

#endif /* SOFTFLOAT_H */
