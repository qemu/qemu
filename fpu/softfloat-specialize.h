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
This C source fragment is part of the SoftFloat IEC/IEEE Floating-point
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

#if defined(TARGET_XTENSA)
/* Define for architectures which deviate from IEEE in not supporting
 * signaling NaNs (so all NaNs are treated as quiet).
 */
#define NO_SIGNALING_NANS 1
#endif

/*----------------------------------------------------------------------------
| The pattern for a default generated half-precision NaN.
*----------------------------------------------------------------------------*/
float16 float16_default_nan(float_status *status)
{
#if defined(TARGET_ARM)
    return const_float16(0x7E00);
#else
    if (status->snan_bit_is_one) {
        return const_float16(0x7DFF);
    } else {
#if defined(TARGET_MIPS)
        return const_float16(0x7E00);
#else
        return const_float16(0xFE00);
#endif
    }
#endif
}

/*----------------------------------------------------------------------------
| The pattern for a default generated single-precision NaN.
*----------------------------------------------------------------------------*/
float32 float32_default_nan(float_status *status)
{
#if defined(TARGET_SPARC)
    return const_float32(0x7FFFFFFF);
#elif defined(TARGET_PPC) || defined(TARGET_ARM) || defined(TARGET_ALPHA) || \
      defined(TARGET_XTENSA) || defined(TARGET_S390X) || defined(TARGET_TRICORE)
    return const_float32(0x7FC00000);
#elif defined(TARGET_HPPA)
    return const_float32(0x7FA00000);
#else
    if (status->snan_bit_is_one) {
        return const_float32(0x7FBFFFFF);
    } else {
#if defined(TARGET_MIPS)
        return const_float32(0x7FC00000);
#else
        return const_float32(0xFFC00000);
#endif
    }
#endif
}

/*----------------------------------------------------------------------------
| The pattern for a default generated double-precision NaN.
*----------------------------------------------------------------------------*/
float64 float64_default_nan(float_status *status)
{
#if defined(TARGET_SPARC)
    return const_float64(LIT64(0x7FFFFFFFFFFFFFFF));
#elif defined(TARGET_PPC) || defined(TARGET_ARM) || defined(TARGET_ALPHA) || \
      defined(TARGET_S390X)
    return const_float64(LIT64(0x7FF8000000000000));
#elif defined(TARGET_HPPA)
    return const_float64(LIT64(0x7FF4000000000000));
#else
    if (status->snan_bit_is_one) {
        return const_float64(LIT64(0x7FF7FFFFFFFFFFFF));
    } else {
#if defined(TARGET_MIPS)
        return const_float64(LIT64(0x7FF8000000000000));
#else
        return const_float64(LIT64(0xFFF8000000000000));
#endif
    }
#endif
}

/*----------------------------------------------------------------------------
| The pattern for a default generated extended double-precision NaN.
*----------------------------------------------------------------------------*/
floatx80 floatx80_default_nan(float_status *status)
{
    floatx80 r;

    if (status->snan_bit_is_one) {
        r.low = LIT64(0xBFFFFFFFFFFFFFFF);
        r.high = 0x7FFF;
    } else {
        r.low = LIT64(0xC000000000000000);
        r.high = 0xFFFF;
    }
    return r;
}

/*----------------------------------------------------------------------------
| The pattern for a default generated quadruple-precision NaN.
*----------------------------------------------------------------------------*/
float128 float128_default_nan(float_status *status)
{
    float128 r;

    if (status->snan_bit_is_one) {
        r.low = LIT64(0xFFFFFFFFFFFFFFFF);
        r.high = LIT64(0x7FFF7FFFFFFFFFFF);
    } else {
        r.low = LIT64(0x0000000000000000);
#if defined(TARGET_S390X) || defined(TARGET_PPC)
        r.high = LIT64(0x7FFF800000000000);
#else
        r.high = LIT64(0xFFFF800000000000);
#endif
    }
    return r;
}

/*----------------------------------------------------------------------------
| Raises the exceptions specified by `flags'.  Floating-point traps can be
| defined here if desired.  It is currently not possible for such a trap
| to substitute a result value.  If traps are not implemented, this routine
| should be simply `float_exception_flags |= flags;'.
*----------------------------------------------------------------------------*/

void float_raise(uint8_t flags, float_status *status)
{
    status->float_exception_flags |= flags;
}

/*----------------------------------------------------------------------------
| Internal canonical NaN format.
*----------------------------------------------------------------------------*/
typedef struct {
    flag sign;
    uint64_t high, low;
} commonNaNT;

#ifdef NO_SIGNALING_NANS
int float16_is_quiet_nan(float16 a_, float_status *status)
{
    return float16_is_any_nan(a_);
}

int float16_is_signaling_nan(float16 a_, float_status *status)
{
    return 0;
}
#else
/*----------------------------------------------------------------------------
| Returns 1 if the half-precision floating-point value `a' is a quiet
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

int float16_is_quiet_nan(float16 a_, float_status *status)
{
    uint16_t a = float16_val(a_);
    if (status->snan_bit_is_one) {
        return (((a >> 9) & 0x3F) == 0x3E) && (a & 0x1FF);
    } else {
        return ((a & ~0x8000) >= 0x7C80);
    }
}

/*----------------------------------------------------------------------------
| Returns 1 if the half-precision floating-point value `a' is a signaling
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

int float16_is_signaling_nan(float16 a_, float_status *status)
{
    uint16_t a = float16_val(a_);
    if (status->snan_bit_is_one) {
        return ((a & ~0x8000) >= 0x7C80);
    } else {
        return (((a >> 9) & 0x3F) == 0x3E) && (a & 0x1FF);
    }
}
#endif

/*----------------------------------------------------------------------------
| Returns a quiet NaN if the half-precision floating point value `a' is a
| signaling NaN; otherwise returns `a'.
*----------------------------------------------------------------------------*/
float16 float16_maybe_silence_nan(float16 a_, float_status *status)
{
    if (float16_is_signaling_nan(a_, status)) {
        if (status->snan_bit_is_one) {
            return float16_default_nan(status);
        } else {
            uint16_t a = float16_val(a_);
            a |= (1 << 9);
            return make_float16(a);
        }
    }
    return a_;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the half-precision floating-point NaN
| `a' to the canonical NaN format.  If `a' is a signaling NaN, the invalid
| exception is raised.
*----------------------------------------------------------------------------*/

static commonNaNT float16ToCommonNaN(float16 a, float_status *status)
{
    commonNaNT z;

    if (float16_is_signaling_nan(a, status)) {
        float_raise(float_flag_invalid, status);
    }
    z.sign = float16_val(a) >> 15;
    z.low = 0;
    z.high = ((uint64_t) float16_val(a)) << 54;
    return z;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the canonical NaN `a' to the half-
| precision floating-point format.
*----------------------------------------------------------------------------*/

static float16 commonNaNToFloat16(commonNaNT a, float_status *status)
{
    uint16_t mantissa = a.high >> 54;

    if (status->default_nan_mode) {
        return float16_default_nan(status);
    }

    if (mantissa) {
        return make_float16(((((uint16_t) a.sign) << 15)
                             | (0x1F << 10) | mantissa));
    } else {
        return float16_default_nan(status);
    }
}

#ifdef NO_SIGNALING_NANS
int float32_is_quiet_nan(float32 a_, float_status *status)
{
    return float32_is_any_nan(a_);
}

int float32_is_signaling_nan(float32 a_, float_status *status)
{
    return 0;
}
#else
/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is a quiet
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

int float32_is_quiet_nan(float32 a_, float_status *status)
{
    uint32_t a = float32_val(a_);
    if (status->snan_bit_is_one) {
        return (((a >> 22) & 0x1FF) == 0x1FE) && (a & 0x003FFFFF);
    } else {
        return ((uint32_t)(a << 1) >= 0xFF800000);
    }
}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is a signaling
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

int float32_is_signaling_nan(float32 a_, float_status *status)
{
    uint32_t a = float32_val(a_);
    if (status->snan_bit_is_one) {
        return ((uint32_t)(a << 1) >= 0xFF800000);
    } else {
        return (((a >> 22) & 0x1FF) == 0x1FE) && (a & 0x003FFFFF);
    }
}
#endif

/*----------------------------------------------------------------------------
| Returns a quiet NaN if the single-precision floating point value `a' is a
| signaling NaN; otherwise returns `a'.
*----------------------------------------------------------------------------*/

float32 float32_maybe_silence_nan(float32 a_, float_status *status)
{
    if (float32_is_signaling_nan(a_, status)) {
        if (status->snan_bit_is_one) {
#ifdef TARGET_HPPA
            uint32_t a = float32_val(a_);
            a &= ~0x00400000;
            a |=  0x00200000;
            return make_float32(a);
#else
            return float32_default_nan(status);
#endif
        } else {
            uint32_t a = float32_val(a_);
            a |= (1 << 22);
            return make_float32(a);
        }
    }
    return a_;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point NaN
| `a' to the canonical NaN format.  If `a' is a signaling NaN, the invalid
| exception is raised.
*----------------------------------------------------------------------------*/

static commonNaNT float32ToCommonNaN(float32 a, float_status *status)
{
    commonNaNT z;

    if (float32_is_signaling_nan(a, status)) {
        float_raise(float_flag_invalid, status);
    }
    z.sign = float32_val(a) >> 31;
    z.low = 0;
    z.high = ((uint64_t)float32_val(a)) << 41;
    return z;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the canonical NaN `a' to the single-
| precision floating-point format.
*----------------------------------------------------------------------------*/

static float32 commonNaNToFloat32(commonNaNT a, float_status *status)
{
    uint32_t mantissa = a.high >> 41;

    if (status->default_nan_mode) {
        return float32_default_nan(status);
    }

    if (mantissa) {
        return make_float32(
            (((uint32_t)a.sign) << 31) | 0x7F800000 | (a.high >> 41));
    } else {
        return float32_default_nan(status);
    }
}

/*----------------------------------------------------------------------------
| Select which NaN to propagate for a two-input operation.
| IEEE754 doesn't specify all the details of this, so the
| algorithm is target-specific.
| The routine is passed various bits of information about the
| two NaNs and should return 0 to select NaN a and 1 for NaN b.
| Note that signalling NaNs are always squashed to quiet NaNs
| by the caller, by calling floatXX_maybe_silence_nan() before
| returning them.
|
| aIsLargerSignificand is only valid if both a and b are NaNs
| of some kind, and is true if a has the larger significand,
| or if both a and b have the same significand but a is
| positive but b is negative. It is only needed for the x87
| tie-break rule.
*----------------------------------------------------------------------------*/

#if defined(TARGET_ARM)
static int pickNaN(flag aIsQNaN, flag aIsSNaN, flag bIsQNaN, flag bIsSNaN,
                    flag aIsLargerSignificand)
{
    /* ARM mandated NaN propagation rules: take the first of:
     *  1. A if it is signaling
     *  2. B if it is signaling
     *  3. A (quiet)
     *  4. B (quiet)
     * A signaling NaN is always quietened before returning it.
     */
    if (aIsSNaN) {
        return 0;
    } else if (bIsSNaN) {
        return 1;
    } else if (aIsQNaN) {
        return 0;
    } else {
        return 1;
    }
}
#elif defined(TARGET_MIPS) || defined(TARGET_HPPA)
static int pickNaN(flag aIsQNaN, flag aIsSNaN, flag bIsQNaN, flag bIsSNaN,
                    flag aIsLargerSignificand)
{
    /* According to MIPS specifications, if one of the two operands is
     * a sNaN, a new qNaN has to be generated. This is done in
     * floatXX_maybe_silence_nan(). For qNaN inputs the specifications
     * says: "When possible, this QNaN result is one of the operand QNaN
     * values." In practice it seems that most implementations choose
     * the first operand if both operands are qNaN. In short this gives
     * the following rules:
     *  1. A if it is signaling
     *  2. B if it is signaling
     *  3. A (quiet)
     *  4. B (quiet)
     * A signaling NaN is always silenced before returning it.
     */
    if (aIsSNaN) {
        return 0;
    } else if (bIsSNaN) {
        return 1;
    } else if (aIsQNaN) {
        return 0;
    } else {
        return 1;
    }
}
#elif defined(TARGET_PPC) || defined(TARGET_XTENSA)
static int pickNaN(flag aIsQNaN, flag aIsSNaN, flag bIsQNaN, flag bIsSNaN,
                   flag aIsLargerSignificand)
{
    /* PowerPC propagation rules:
     *  1. A if it sNaN or qNaN
     *  2. B if it sNaN or qNaN
     * A signaling NaN is always silenced before returning it.
     */
    if (aIsSNaN || aIsQNaN) {
        return 0;
    } else {
        return 1;
    }
}
#else
static int pickNaN(flag aIsQNaN, flag aIsSNaN, flag bIsQNaN, flag bIsSNaN,
                    flag aIsLargerSignificand)
{
    /* This implements x87 NaN propagation rules:
     * SNaN + QNaN => return the QNaN
     * two SNaNs => return the one with the larger significand, silenced
     * two QNaNs => return the one with the larger significand
     * SNaN and a non-NaN => return the SNaN, silenced
     * QNaN and a non-NaN => return the QNaN
     *
     * If we get down to comparing significands and they are the same,
     * return the NaN with the positive sign bit (if any).
     */
    if (aIsSNaN) {
        if (bIsSNaN) {
            return aIsLargerSignificand ? 0 : 1;
        }
        return bIsQNaN ? 1 : 0;
    } else if (aIsQNaN) {
        if (bIsSNaN || !bIsQNaN) {
            return 0;
        } else {
            return aIsLargerSignificand ? 0 : 1;
        }
    } else {
        return 1;
    }
}
#endif

/*----------------------------------------------------------------------------
| Select which NaN to propagate for a three-input operation.
| For the moment we assume that no CPU needs the 'larger significand'
| information.
| Return values : 0 : a; 1 : b; 2 : c; 3 : default-NaN
*----------------------------------------------------------------------------*/
#if defined(TARGET_ARM)
static int pickNaNMulAdd(flag aIsQNaN, flag aIsSNaN, flag bIsQNaN, flag bIsSNaN,
                         flag cIsQNaN, flag cIsSNaN, flag infzero,
                         float_status *status)
{
    /* For ARM, the (inf,zero,qnan) case sets InvalidOp and returns
     * the default NaN
     */
    if (infzero && cIsQNaN) {
        float_raise(float_flag_invalid, status);
        return 3;
    }

    /* This looks different from the ARM ARM pseudocode, because the ARM ARM
     * puts the operands to a fused mac operation (a*b)+c in the order c,a,b.
     */
    if (cIsSNaN) {
        return 2;
    } else if (aIsSNaN) {
        return 0;
    } else if (bIsSNaN) {
        return 1;
    } else if (cIsQNaN) {
        return 2;
    } else if (aIsQNaN) {
        return 0;
    } else {
        return 1;
    }
}
#elif defined(TARGET_MIPS)
static int pickNaNMulAdd(flag aIsQNaN, flag aIsSNaN, flag bIsQNaN, flag bIsSNaN,
                         flag cIsQNaN, flag cIsSNaN, flag infzero,
                         float_status *status)
{
    /* For MIPS, the (inf,zero,qnan) case sets InvalidOp and returns
     * the default NaN
     */
    if (infzero) {
        float_raise(float_flag_invalid, status);
        return 3;
    }

    if (status->snan_bit_is_one) {
        /* Prefer sNaN over qNaN, in the a, b, c order. */
        if (aIsSNaN) {
            return 0;
        } else if (bIsSNaN) {
            return 1;
        } else if (cIsSNaN) {
            return 2;
        } else if (aIsQNaN) {
            return 0;
        } else if (bIsQNaN) {
            return 1;
        } else {
            return 2;
        }
    } else {
        /* Prefer sNaN over qNaN, in the c, a, b order. */
        if (cIsSNaN) {
            return 2;
        } else if (aIsSNaN) {
            return 0;
        } else if (bIsSNaN) {
            return 1;
        } else if (cIsQNaN) {
            return 2;
        } else if (aIsQNaN) {
            return 0;
        } else {
            return 1;
        }
    }
}
#elif defined(TARGET_PPC)
static int pickNaNMulAdd(flag aIsQNaN, flag aIsSNaN, flag bIsQNaN, flag bIsSNaN,
                         flag cIsQNaN, flag cIsSNaN, flag infzero,
                         float_status *status)
{
    /* For PPC, the (inf,zero,qnan) case sets InvalidOp, but we prefer
     * to return an input NaN if we have one (ie c) rather than generating
     * a default NaN
     */
    if (infzero) {
        float_raise(float_flag_invalid, status);
        return 2;
    }

    /* If fRA is a NaN return it; otherwise if fRB is a NaN return it;
     * otherwise return fRC. Note that muladd on PPC is (fRA * fRC) + frB
     */
    if (aIsSNaN || aIsQNaN) {
        return 0;
    } else if (cIsSNaN || cIsQNaN) {
        return 2;
    } else {
        return 1;
    }
}
#else
/* A default implementation: prefer a to b to c.
 * This is unlikely to actually match any real implementation.
 */
static int pickNaNMulAdd(flag aIsQNaN, flag aIsSNaN, flag bIsQNaN, flag bIsSNaN,
                         flag cIsQNaN, flag cIsSNaN, flag infzero,
                         float_status *status)
{
    if (aIsSNaN || aIsQNaN) {
        return 0;
    } else if (bIsSNaN || bIsQNaN) {
        return 1;
    } else {
        return 2;
    }
}
#endif

/*----------------------------------------------------------------------------
| Takes two single-precision floating-point values `a' and `b', one of which
| is a NaN, and returns the appropriate NaN result.  If either `a' or `b' is a
| signaling NaN, the invalid exception is raised.
*----------------------------------------------------------------------------*/

static float32 propagateFloat32NaN(float32 a, float32 b, float_status *status)
{
    flag aIsQuietNaN, aIsSignalingNaN, bIsQuietNaN, bIsSignalingNaN;
    flag aIsLargerSignificand;
    uint32_t av, bv;

    aIsQuietNaN = float32_is_quiet_nan(a, status);
    aIsSignalingNaN = float32_is_signaling_nan(a, status);
    bIsQuietNaN = float32_is_quiet_nan(b, status);
    bIsSignalingNaN = float32_is_signaling_nan(b, status);
    av = float32_val(a);
    bv = float32_val(b);

    if (aIsSignalingNaN | bIsSignalingNaN) {
        float_raise(float_flag_invalid, status);
    }

    if (status->default_nan_mode) {
        return float32_default_nan(status);
    }

    if ((uint32_t)(av << 1) < (uint32_t)(bv << 1)) {
        aIsLargerSignificand = 0;
    } else if ((uint32_t)(bv << 1) < (uint32_t)(av << 1)) {
        aIsLargerSignificand = 1;
    } else {
        aIsLargerSignificand = (av < bv) ? 1 : 0;
    }

    if (pickNaN(aIsQuietNaN, aIsSignalingNaN, bIsQuietNaN, bIsSignalingNaN,
                aIsLargerSignificand)) {
        return float32_maybe_silence_nan(b, status);
    } else {
        return float32_maybe_silence_nan(a, status);
    }
}

/*----------------------------------------------------------------------------
| Takes three single-precision floating-point values `a', `b' and `c', one of
| which is a NaN, and returns the appropriate NaN result.  If any of  `a',
| `b' or `c' is a signaling NaN, the invalid exception is raised.
| The input infzero indicates whether a*b was 0*inf or inf*0 (in which case
| obviously c is a NaN, and whether to propagate c or some other NaN is
| implementation defined).
*----------------------------------------------------------------------------*/

static float32 propagateFloat32MulAddNaN(float32 a, float32 b,
                                         float32 c, flag infzero,
                                         float_status *status)
{
    flag aIsQuietNaN, aIsSignalingNaN, bIsQuietNaN, bIsSignalingNaN,
        cIsQuietNaN, cIsSignalingNaN;
    int which;

    aIsQuietNaN = float32_is_quiet_nan(a, status);
    aIsSignalingNaN = float32_is_signaling_nan(a, status);
    bIsQuietNaN = float32_is_quiet_nan(b, status);
    bIsSignalingNaN = float32_is_signaling_nan(b, status);
    cIsQuietNaN = float32_is_quiet_nan(c, status);
    cIsSignalingNaN = float32_is_signaling_nan(c, status);

    if (aIsSignalingNaN | bIsSignalingNaN | cIsSignalingNaN) {
        float_raise(float_flag_invalid, status);
    }

    which = pickNaNMulAdd(aIsQuietNaN, aIsSignalingNaN,
                          bIsQuietNaN, bIsSignalingNaN,
                          cIsQuietNaN, cIsSignalingNaN, infzero, status);

    if (status->default_nan_mode) {
        /* Note that this check is after pickNaNMulAdd so that function
         * has an opportunity to set the Invalid flag.
         */
        return float32_default_nan(status);
    }

    switch (which) {
    case 0:
        return float32_maybe_silence_nan(a, status);
    case 1:
        return float32_maybe_silence_nan(b, status);
    case 2:
        return float32_maybe_silence_nan(c, status);
    case 3:
    default:
        return float32_default_nan(status);
    }
}

#ifdef NO_SIGNALING_NANS
int float64_is_quiet_nan(float64 a_, float_status *status)
{
    return float64_is_any_nan(a_);
}

int float64_is_signaling_nan(float64 a_, float_status *status)
{
    return 0;
}
#else
/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point value `a' is a quiet
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

int float64_is_quiet_nan(float64 a_, float_status *status)
{
    uint64_t a = float64_val(a_);
    if (status->snan_bit_is_one) {
        return (((a >> 51) & 0xFFF) == 0xFFE)
            && (a & 0x0007FFFFFFFFFFFFULL);
    } else {
        return ((a << 1) >= 0xFFF0000000000000ULL);
    }
}

/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point value `a' is a signaling
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

int float64_is_signaling_nan(float64 a_, float_status *status)
{
    uint64_t a = float64_val(a_);
    if (status->snan_bit_is_one) {
        return ((a << 1) >= 0xFFF0000000000000ULL);
    } else {
        return (((a >> 51) & 0xFFF) == 0xFFE)
            && (a & LIT64(0x0007FFFFFFFFFFFF));
    }
}
#endif

/*----------------------------------------------------------------------------
| Returns a quiet NaN if the double-precision floating point value `a' is a
| signaling NaN; otherwise returns `a'.
*----------------------------------------------------------------------------*/

float64 float64_maybe_silence_nan(float64 a_, float_status *status)
{
    if (float64_is_signaling_nan(a_, status)) {
        if (status->snan_bit_is_one) {
#ifdef TARGET_HPPA
            uint64_t a = float64_val(a_);
            a &= ~0x0008000000000000ULL;
            a |=  0x0004000000000000ULL;
            return make_float64(a);
#else
            return float64_default_nan(status);
#endif
        } else {
            uint64_t a = float64_val(a_);
            a |= LIT64(0x0008000000000000);
            return make_float64(a);
        }
    }
    return a_;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-precision floating-point NaN
| `a' to the canonical NaN format.  If `a' is a signaling NaN, the invalid
| exception is raised.
*----------------------------------------------------------------------------*/

static commonNaNT float64ToCommonNaN(float64 a, float_status *status)
{
    commonNaNT z;

    if (float64_is_signaling_nan(a, status)) {
        float_raise(float_flag_invalid, status);
    }
    z.sign = float64_val(a) >> 63;
    z.low = 0;
    z.high = float64_val(a) << 12;
    return z;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the canonical NaN `a' to the double-
| precision floating-point format.
*----------------------------------------------------------------------------*/

static float64 commonNaNToFloat64(commonNaNT a, float_status *status)
{
    uint64_t mantissa = a.high >> 12;

    if (status->default_nan_mode) {
        return float64_default_nan(status);
    }

    if (mantissa) {
        return make_float64(
              (((uint64_t) a.sign) << 63)
            | LIT64(0x7FF0000000000000)
            | (a.high >> 12));
    } else {
        return float64_default_nan(status);
    }
}

/*----------------------------------------------------------------------------
| Takes two double-precision floating-point values `a' and `b', one of which
| is a NaN, and returns the appropriate NaN result.  If either `a' or `b' is a
| signaling NaN, the invalid exception is raised.
*----------------------------------------------------------------------------*/

static float64 propagateFloat64NaN(float64 a, float64 b, float_status *status)
{
    flag aIsQuietNaN, aIsSignalingNaN, bIsQuietNaN, bIsSignalingNaN;
    flag aIsLargerSignificand;
    uint64_t av, bv;

    aIsQuietNaN = float64_is_quiet_nan(a, status);
    aIsSignalingNaN = float64_is_signaling_nan(a, status);
    bIsQuietNaN = float64_is_quiet_nan(b, status);
    bIsSignalingNaN = float64_is_signaling_nan(b, status);
    av = float64_val(a);
    bv = float64_val(b);

    if (aIsSignalingNaN | bIsSignalingNaN) {
        float_raise(float_flag_invalid, status);
    }

    if (status->default_nan_mode) {
        return float64_default_nan(status);
    }

    if ((uint64_t)(av << 1) < (uint64_t)(bv << 1)) {
        aIsLargerSignificand = 0;
    } else if ((uint64_t)(bv << 1) < (uint64_t)(av << 1)) {
        aIsLargerSignificand = 1;
    } else {
        aIsLargerSignificand = (av < bv) ? 1 : 0;
    }

    if (pickNaN(aIsQuietNaN, aIsSignalingNaN, bIsQuietNaN, bIsSignalingNaN,
                aIsLargerSignificand)) {
        return float64_maybe_silence_nan(b, status);
    } else {
        return float64_maybe_silence_nan(a, status);
    }
}

/*----------------------------------------------------------------------------
| Takes three double-precision floating-point values `a', `b' and `c', one of
| which is a NaN, and returns the appropriate NaN result.  If any of  `a',
| `b' or `c' is a signaling NaN, the invalid exception is raised.
| The input infzero indicates whether a*b was 0*inf or inf*0 (in which case
| obviously c is a NaN, and whether to propagate c or some other NaN is
| implementation defined).
*----------------------------------------------------------------------------*/

static float64 propagateFloat64MulAddNaN(float64 a, float64 b,
                                         float64 c, flag infzero,
                                         float_status *status)
{
    flag aIsQuietNaN, aIsSignalingNaN, bIsQuietNaN, bIsSignalingNaN,
        cIsQuietNaN, cIsSignalingNaN;
    int which;

    aIsQuietNaN = float64_is_quiet_nan(a, status);
    aIsSignalingNaN = float64_is_signaling_nan(a, status);
    bIsQuietNaN = float64_is_quiet_nan(b, status);
    bIsSignalingNaN = float64_is_signaling_nan(b, status);
    cIsQuietNaN = float64_is_quiet_nan(c, status);
    cIsSignalingNaN = float64_is_signaling_nan(c, status);

    if (aIsSignalingNaN | bIsSignalingNaN | cIsSignalingNaN) {
        float_raise(float_flag_invalid, status);
    }

    which = pickNaNMulAdd(aIsQuietNaN, aIsSignalingNaN,
                          bIsQuietNaN, bIsSignalingNaN,
                          cIsQuietNaN, cIsSignalingNaN, infzero, status);

    if (status->default_nan_mode) {
        /* Note that this check is after pickNaNMulAdd so that function
         * has an opportunity to set the Invalid flag.
         */
        return float64_default_nan(status);
    }

    switch (which) {
    case 0:
        return float64_maybe_silence_nan(a, status);
    case 1:
        return float64_maybe_silence_nan(b, status);
    case 2:
        return float64_maybe_silence_nan(c, status);
    case 3:
    default:
        return float64_default_nan(status);
    }
}

#ifdef NO_SIGNALING_NANS
int floatx80_is_quiet_nan(floatx80 a_, float_status *status)
{
    return floatx80_is_any_nan(a_);
}

int floatx80_is_signaling_nan(floatx80 a_, float_status *status)
{
    return 0;
}
#else
/*----------------------------------------------------------------------------
| Returns 1 if the extended double-precision floating-point value `a' is a
| quiet NaN; otherwise returns 0. This slightly differs from the same
| function for other types as floatx80 has an explicit bit.
*----------------------------------------------------------------------------*/

int floatx80_is_quiet_nan(floatx80 a, float_status *status)
{
    if (status->snan_bit_is_one) {
        uint64_t aLow;

        aLow = a.low & ~0x4000000000000000ULL;
        return ((a.high & 0x7FFF) == 0x7FFF)
            && (aLow << 1)
            && (a.low == aLow);
    } else {
        return ((a.high & 0x7FFF) == 0x7FFF)
            && (LIT64(0x8000000000000000) <= ((uint64_t)(a.low << 1)));
    }
}

/*----------------------------------------------------------------------------
| Returns 1 if the extended double-precision floating-point value `a' is a
| signaling NaN; otherwise returns 0. This slightly differs from the same
| function for other types as floatx80 has an explicit bit.
*----------------------------------------------------------------------------*/

int floatx80_is_signaling_nan(floatx80 a, float_status *status)
{
    if (status->snan_bit_is_one) {
        return ((a.high & 0x7FFF) == 0x7FFF)
            && ((a.low << 1) >= 0x8000000000000000ULL);
    } else {
        uint64_t aLow;

        aLow = a.low & ~LIT64(0x4000000000000000);
        return ((a.high & 0x7FFF) == 0x7FFF)
            && (uint64_t)(aLow << 1)
            && (a.low == aLow);
    }
}
#endif

/*----------------------------------------------------------------------------
| Returns a quiet NaN if the extended double-precision floating point value
| `a' is a signaling NaN; otherwise returns `a'.
*----------------------------------------------------------------------------*/

floatx80 floatx80_maybe_silence_nan(floatx80 a, float_status *status)
{
    if (floatx80_is_signaling_nan(a, status)) {
        if (status->snan_bit_is_one) {
            a = floatx80_default_nan(status);
        } else {
            a.low |= LIT64(0xC000000000000000);
            return a;
        }
    }
    return a;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point NaN `a' to the canonical NaN format.  If `a' is a signaling NaN, the
| invalid exception is raised.
*----------------------------------------------------------------------------*/

static commonNaNT floatx80ToCommonNaN(floatx80 a, float_status *status)
{
    floatx80 dflt;
    commonNaNT z;

    if (floatx80_is_signaling_nan(a, status)) {
        float_raise(float_flag_invalid, status);
    }
    if (a.low >> 63) {
        z.sign = a.high >> 15;
        z.low = 0;
        z.high = a.low << 1;
    } else {
        dflt = floatx80_default_nan(status);
        z.sign = dflt.high >> 15;
        z.low = 0;
        z.high = dflt.low << 1;
    }
    return z;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the canonical NaN `a' to the extended
| double-precision floating-point format.
*----------------------------------------------------------------------------*/

static floatx80 commonNaNToFloatx80(commonNaNT a, float_status *status)
{
    floatx80 z;

    if (status->default_nan_mode) {
        return floatx80_default_nan(status);
    }

    if (a.high >> 1) {
        z.low = LIT64(0x8000000000000000) | a.high >> 1;
        z.high = (((uint16_t)a.sign) << 15) | 0x7FFF;
    } else {
        z = floatx80_default_nan(status);
    }
    return z;
}

/*----------------------------------------------------------------------------
| Takes two extended double-precision floating-point values `a' and `b', one
| of which is a NaN, and returns the appropriate NaN result.  If either `a' or
| `b' is a signaling NaN, the invalid exception is raised.
*----------------------------------------------------------------------------*/

static floatx80 propagateFloatx80NaN(floatx80 a, floatx80 b,
                                     float_status *status)
{
    flag aIsQuietNaN, aIsSignalingNaN, bIsQuietNaN, bIsSignalingNaN;
    flag aIsLargerSignificand;

    aIsQuietNaN = floatx80_is_quiet_nan(a, status);
    aIsSignalingNaN = floatx80_is_signaling_nan(a, status);
    bIsQuietNaN = floatx80_is_quiet_nan(b, status);
    bIsSignalingNaN = floatx80_is_signaling_nan(b, status);

    if (aIsSignalingNaN | bIsSignalingNaN) {
        float_raise(float_flag_invalid, status);
    }

    if (status->default_nan_mode) {
        return floatx80_default_nan(status);
    }

    if (a.low < b.low) {
        aIsLargerSignificand = 0;
    } else if (b.low < a.low) {
        aIsLargerSignificand = 1;
    } else {
        aIsLargerSignificand = (a.high < b.high) ? 1 : 0;
    }

    if (pickNaN(aIsQuietNaN, aIsSignalingNaN, bIsQuietNaN, bIsSignalingNaN,
                aIsLargerSignificand)) {
        return floatx80_maybe_silence_nan(b, status);
    } else {
        return floatx80_maybe_silence_nan(a, status);
    }
}

#ifdef NO_SIGNALING_NANS
int float128_is_quiet_nan(float128 a_, float_status *status)
{
    return float128_is_any_nan(a_);
}

int float128_is_signaling_nan(float128 a_, float_status *status)
{
    return 0;
}
#else
/*----------------------------------------------------------------------------
| Returns 1 if the quadruple-precision floating-point value `a' is a quiet
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

int float128_is_quiet_nan(float128 a, float_status *status)
{
    if (status->snan_bit_is_one) {
        return (((a.high >> 47) & 0xFFFF) == 0xFFFE)
            && (a.low || (a.high & 0x00007FFFFFFFFFFFULL));
    } else {
        return ((a.high << 1) >= 0xFFFF000000000000ULL)
            && (a.low || (a.high & 0x0000FFFFFFFFFFFFULL));
    }
}

/*----------------------------------------------------------------------------
| Returns 1 if the quadruple-precision floating-point value `a' is a
| signaling NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

int float128_is_signaling_nan(float128 a, float_status *status)
{
    if (status->snan_bit_is_one) {
        return ((a.high << 1) >= 0xFFFF000000000000ULL)
            && (a.low || (a.high & 0x0000FFFFFFFFFFFFULL));
    } else {
        return (((a.high >> 47) & 0xFFFF) == 0xFFFE)
            && (a.low || (a.high & LIT64(0x00007FFFFFFFFFFF)));
    }
}
#endif

/*----------------------------------------------------------------------------
| Returns a quiet NaN if the quadruple-precision floating point value `a' is
| a signaling NaN; otherwise returns `a'.
*----------------------------------------------------------------------------*/

float128 float128_maybe_silence_nan(float128 a, float_status *status)
{
    if (float128_is_signaling_nan(a, status)) {
        if (status->snan_bit_is_one) {
            a = float128_default_nan(status);
        } else {
            a.high |= LIT64(0x0000800000000000);
            return a;
        }
    }
    return a;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the quadruple-precision floating-point NaN
| `a' to the canonical NaN format.  If `a' is a signaling NaN, the invalid
| exception is raised.
*----------------------------------------------------------------------------*/

static commonNaNT float128ToCommonNaN(float128 a, float_status *status)
{
    commonNaNT z;

    if (float128_is_signaling_nan(a, status)) {
        float_raise(float_flag_invalid, status);
    }
    z.sign = a.high >> 63;
    shortShift128Left(a.high, a.low, 16, &z.high, &z.low);
    return z;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the canonical NaN `a' to the quadruple-
| precision floating-point format.
*----------------------------------------------------------------------------*/

static float128 commonNaNToFloat128(commonNaNT a, float_status *status)
{
    float128 z;

    if (status->default_nan_mode) {
        return float128_default_nan(status);
    }

    shift128Right(a.high, a.low, 16, &z.high, &z.low);
    z.high |= (((uint64_t)a.sign) << 63) | LIT64(0x7FFF000000000000);
    return z;
}

/*----------------------------------------------------------------------------
| Takes two quadruple-precision floating-point values `a' and `b', one of
| which is a NaN, and returns the appropriate NaN result.  If either `a' or
| `b' is a signaling NaN, the invalid exception is raised.
*----------------------------------------------------------------------------*/

static float128 propagateFloat128NaN(float128 a, float128 b,
                                     float_status *status)
{
    flag aIsQuietNaN, aIsSignalingNaN, bIsQuietNaN, bIsSignalingNaN;
    flag aIsLargerSignificand;

    aIsQuietNaN = float128_is_quiet_nan(a, status);
    aIsSignalingNaN = float128_is_signaling_nan(a, status);
    bIsQuietNaN = float128_is_quiet_nan(b, status);
    bIsSignalingNaN = float128_is_signaling_nan(b, status);

    if (aIsSignalingNaN | bIsSignalingNaN) {
        float_raise(float_flag_invalid, status);
    }

    if (status->default_nan_mode) {
        return float128_default_nan(status);
    }

    if (lt128(a.high << 1, a.low, b.high << 1, b.low)) {
        aIsLargerSignificand = 0;
    } else if (lt128(b.high << 1, b.low, a.high << 1, a.low)) {
        aIsLargerSignificand = 1;
    } else {
        aIsLargerSignificand = (a.high < b.high) ? 1 : 0;
    }

    if (pickNaN(aIsQuietNaN, aIsSignalingNaN, bIsQuietNaN, bIsSignalingNaN,
                aIsLargerSignificand)) {
        return float128_maybe_silence_nan(b, status);
    } else {
        return float128_maybe_silence_nan(a, status);
    }
}
