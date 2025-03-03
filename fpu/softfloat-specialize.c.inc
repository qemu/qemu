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

/*
 * Define whether architecture deviates from IEEE in not supporting
 * signaling NaNs (so all NaNs are treated as quiet).
 */
static inline bool no_signaling_nans(float_status *status)
{
    return status->no_signaling_nans;
}

/* Define how the architecture discriminates signaling NaNs.
 * This done with the most significant bit of the fraction.
 * In IEEE 754-1985 this was implementation defined, but in IEEE 754-2008
 * the msb must be zero.  MIPS is (so far) unique in supporting both the
 * 2008 revision and backward compatibility with their original choice.
 */
static inline bool snan_bit_is_one(float_status *status)
{
    return status->snan_bit_is_one;
}

/*----------------------------------------------------------------------------
| For the deconstructed floating-point with fraction FRAC, return true
| if the fraction represents a signalling NaN; otherwise false.
*----------------------------------------------------------------------------*/

static bool parts_is_snan_frac(uint64_t frac, float_status *status)
{
    if (no_signaling_nans(status)) {
        return false;
    } else {
        bool msb = extract64(frac, DECOMPOSED_BINARY_POINT - 1, 1);
        return msb == snan_bit_is_one(status);
    }
}

/*----------------------------------------------------------------------------
| The pattern for a default generated deconstructed floating-point NaN.
*----------------------------------------------------------------------------*/

static void parts64_default_nan(FloatParts64 *p, float_status *status)
{
    bool sign = 0;
    uint64_t frac;
    uint8_t dnan_pattern = status->default_nan_pattern;

    assert(dnan_pattern != 0);

    sign = dnan_pattern >> 7;
    /*
     * Place default_nan_pattern [6:0] into bits [62:56],
     * and replecate bit [0] down into [55:0]
     */
    frac = deposit64(0, DECOMPOSED_BINARY_POINT - 7, 7, dnan_pattern);
    frac = deposit64(frac, 0, DECOMPOSED_BINARY_POINT - 7, -(dnan_pattern & 1));

    *p = (FloatParts64) {
        .cls = float_class_qnan,
        .sign = sign,
        .exp = INT_MAX,
        .frac = frac
    };
}

static void parts128_default_nan(FloatParts128 *p, float_status *status)
{
    /*
     * Extrapolate from the choices made by parts64_default_nan to fill
     * in the quad-floating format.  If the low bit is set, assume we
     * want to set all non-snan bits.
     */
    FloatParts64 p64;
    parts64_default_nan(&p64, status);

    *p = (FloatParts128) {
        .cls = float_class_qnan,
        .sign = p64.sign,
        .exp = INT_MAX,
        .frac_hi = p64.frac,
        .frac_lo = -(p64.frac & 1)
    };
}

/*----------------------------------------------------------------------------
| Returns a quiet NaN from a signalling NaN for the deconstructed
| floating-point parts.
*----------------------------------------------------------------------------*/

static uint64_t parts_silence_nan_frac(uint64_t frac, float_status *status)
{
    g_assert(!no_signaling_nans(status));

    /* The only snan_bit_is_one target without default_nan_mode is HPPA. */
    if (snan_bit_is_one(status)) {
        frac &= ~(1ULL << (DECOMPOSED_BINARY_POINT - 1));
        frac |= 1ULL << (DECOMPOSED_BINARY_POINT - 2);
    } else {
        frac |= 1ULL << (DECOMPOSED_BINARY_POINT - 1);
    }
    return frac;
}

static void parts64_silence_nan(FloatParts64 *p, float_status *status)
{
    p->frac = parts_silence_nan_frac(p->frac, status);
    p->cls = float_class_qnan;
}

static void parts128_silence_nan(FloatParts128 *p, float_status *status)
{
    p->frac_hi = parts_silence_nan_frac(p->frac_hi, status);
    p->cls = float_class_qnan;
}

/*----------------------------------------------------------------------------
| The pattern for a default generated extended double-precision NaN.
*----------------------------------------------------------------------------*/
floatx80 floatx80_default_nan(float_status *status)
{
    floatx80 r;
    /*
     * Extrapolate from the choices made by parts64_default_nan to fill
     * in the floatx80 format. We assume that floatx80's explicit
     * integer bit is always set (this is true for i386 and m68k,
     * which are the only real users of this format).
     */
    FloatParts64 p64;
    parts64_default_nan(&p64, status);

    r.high = 0x7FFF | (p64.sign << 15);
    r.low = (1ULL << DECOMPOSED_BINARY_POINT) | p64.frac;
    return r;
}

/*----------------------------------------------------------------------------
| The pattern for a default generated extended double-precision inf.
*----------------------------------------------------------------------------*/

floatx80 floatx80_default_inf(bool zSign, float_status *status)
{
    /*
     * Whether the Integer bit is set in the default Infinity is
     * target dependent.
     */
    bool z = status->floatx80_behaviour & floatx80_default_inf_int_bit_is_zero;
    return packFloatx80(zSign, 0x7fff, z ? 0 : (1ULL << 63));
}

/*----------------------------------------------------------------------------
| Returns 1 if the half-precision floating-point value `a' is a quiet
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

bool float16_is_quiet_nan(float16 a_, float_status *status)
{
    if (no_signaling_nans(status)) {
        return float16_is_any_nan(a_);
    } else {
        uint16_t a = float16_val(a_);
        if (snan_bit_is_one(status)) {
            return (((a >> 9) & 0x3F) == 0x3E) && (a & 0x1FF);
        } else {

            return ((a >> 9) & 0x3F) == 0x3F;
        }
    }
}

/*----------------------------------------------------------------------------
| Returns 1 if the bfloat16 value `a' is a quiet
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

bool bfloat16_is_quiet_nan(bfloat16 a_, float_status *status)
{
    if (no_signaling_nans(status)) {
        return bfloat16_is_any_nan(a_);
    } else {
        uint16_t a = a_;
        if (snan_bit_is_one(status)) {
            return (((a >> 6) & 0x1FF) == 0x1FE) && (a & 0x3F);
        } else {
            return ((a >> 6) & 0x1FF) == 0x1FF;
        }
    }
}

/*----------------------------------------------------------------------------
| Returns 1 if the half-precision floating-point value `a' is a signaling
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

bool float16_is_signaling_nan(float16 a_, float_status *status)
{
    if (no_signaling_nans(status)) {
        return 0;
    } else {
        uint16_t a = float16_val(a_);
        if (snan_bit_is_one(status)) {
            return ((a >> 9) & 0x3F) == 0x3F;
        } else {
            return (((a >> 9) & 0x3F) == 0x3E) && (a & 0x1FF);
        }
    }
}

/*----------------------------------------------------------------------------
| Returns 1 if the bfloat16 value `a' is a signaling
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

bool bfloat16_is_signaling_nan(bfloat16 a_, float_status *status)
{
    if (no_signaling_nans(status)) {
        return 0;
    } else {
        uint16_t a = a_;
        if (snan_bit_is_one(status)) {
            return ((a >> 6) & 0x1FF) == 0x1FF;
        } else {
            return (((a >> 6) & 0x1FF) == 0x1FE) && (a & 0x3F);
        }
    }
}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is a quiet
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

bool float32_is_quiet_nan(float32 a_, float_status *status)
{
    if (no_signaling_nans(status)) {
        return float32_is_any_nan(a_);
    } else {
        uint32_t a = float32_val(a_);
        if (snan_bit_is_one(status)) {
            return (((a >> 22) & 0x1FF) == 0x1FE) && (a & 0x003FFFFF);
        } else {
            return ((uint32_t)(a << 1) >= 0xFF800000);
        }
    }
}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is a signaling
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

bool float32_is_signaling_nan(float32 a_, float_status *status)
{
    if (no_signaling_nans(status)) {
        return 0;
    } else {
        uint32_t a = float32_val(a_);
        if (snan_bit_is_one(status)) {
            return ((uint32_t)(a << 1) >= 0xFF800000);
        } else {
            return (((a >> 22) & 0x1FF) == 0x1FE) && (a & 0x003FFFFF);
        }
    }
}

/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point value `a' is a quiet
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

bool float64_is_quiet_nan(float64 a_, float_status *status)
{
    if (no_signaling_nans(status)) {
        return float64_is_any_nan(a_);
    } else {
        uint64_t a = float64_val(a_);
        if (snan_bit_is_one(status)) {
            return (((a >> 51) & 0xFFF) == 0xFFE)
                && (a & 0x0007FFFFFFFFFFFFULL);
        } else {
            return ((a << 1) >= 0xFFF0000000000000ULL);
        }
    }
}

/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point value `a' is a signaling
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

bool float64_is_signaling_nan(float64 a_, float_status *status)
{
    if (no_signaling_nans(status)) {
        return 0;
    } else {
        uint64_t a = float64_val(a_);
        if (snan_bit_is_one(status)) {
            return ((a << 1) >= 0xFFF0000000000000ULL);
        } else {
            return (((a >> 51) & 0xFFF) == 0xFFE)
                && (a & UINT64_C(0x0007FFFFFFFFFFFF));
        }
    }
}

/*----------------------------------------------------------------------------
| Returns 1 if the extended double-precision floating-point value `a' is a
| quiet NaN; otherwise returns 0. This slightly differs from the same
| function for other types as floatx80 has an explicit bit.
*----------------------------------------------------------------------------*/

int floatx80_is_quiet_nan(floatx80 a, float_status *status)
{
    if (no_signaling_nans(status)) {
        return floatx80_is_any_nan(a);
    } else {
        if (snan_bit_is_one(status)) {
            uint64_t aLow;

            aLow = a.low & ~0x4000000000000000ULL;
            return ((a.high & 0x7FFF) == 0x7FFF)
                && (aLow << 1)
                && (a.low == aLow);
        } else {
            return ((a.high & 0x7FFF) == 0x7FFF)
                && (UINT64_C(0x8000000000000000) <= ((uint64_t)(a.low << 1)));
        }
    }
}

/*----------------------------------------------------------------------------
| Returns 1 if the extended double-precision floating-point value `a' is a
| signaling NaN; otherwise returns 0. This slightly differs from the same
| function for other types as floatx80 has an explicit bit.
*----------------------------------------------------------------------------*/

int floatx80_is_signaling_nan(floatx80 a, float_status *status)
{
    if (no_signaling_nans(status)) {
        return 0;
    } else {
        if (snan_bit_is_one(status)) {
            return ((a.high & 0x7FFF) == 0x7FFF)
                && ((a.low << 1) >= 0x8000000000000000ULL);
        } else {
            uint64_t aLow;

            aLow = a.low & ~UINT64_C(0x4000000000000000);
            return ((a.high & 0x7FFF) == 0x7FFF)
                && (uint64_t)(aLow << 1)
                && (a.low == aLow);
        }
    }
}

/*----------------------------------------------------------------------------
| Returns a quiet NaN from a signalling NaN for the extended double-precision
| floating point value `a'.
*----------------------------------------------------------------------------*/

floatx80 floatx80_silence_nan(floatx80 a, float_status *status)
{
    /* None of the targets that have snan_bit_is_one use floatx80.  */
    assert(!snan_bit_is_one(status));
    a.low |= UINT64_C(0xC000000000000000);
    return a;
}

/*----------------------------------------------------------------------------
| Returns 1 if the quadruple-precision floating-point value `a' is a quiet
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

bool float128_is_quiet_nan(float128 a, float_status *status)
{
    if (no_signaling_nans(status)) {
        return float128_is_any_nan(a);
    } else {
        if (snan_bit_is_one(status)) {
            return (((a.high >> 47) & 0xFFFF) == 0xFFFE)
                && (a.low || (a.high & 0x00007FFFFFFFFFFFULL));
        } else {
            return ((a.high << 1) >= 0xFFFF000000000000ULL)
                && (a.low || (a.high & 0x0000FFFFFFFFFFFFULL));
        }
    }
}

/*----------------------------------------------------------------------------
| Returns 1 if the quadruple-precision floating-point value `a' is a
| signaling NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

bool float128_is_signaling_nan(float128 a, float_status *status)
{
    if (no_signaling_nans(status)) {
        return 0;
    } else {
        if (snan_bit_is_one(status)) {
            return ((a.high << 1) >= 0xFFFF000000000000ULL)
                && (a.low || (a.high & 0x0000FFFFFFFFFFFFULL));
        } else {
            return (((a.high >> 47) & 0xFFFF) == 0xFFFE)
                && (a.low || (a.high & UINT64_C(0x00007FFFFFFFFFFF)));
        }
    }
}
