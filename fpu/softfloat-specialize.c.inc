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
#if defined(TARGET_XTENSA)
    return status->no_signaling_nans;
#else
    return false;
#endif
}

/* Define how the architecture discriminates signaling NaNs.
 * This done with the most significant bit of the fraction.
 * In IEEE 754-1985 this was implementation defined, but in IEEE 754-2008
 * the msb must be zero.  MIPS is (so far) unique in supporting both the
 * 2008 revision and backward compatibility with their original choice.
 * Thus for MIPS we must make the choice at runtime.
 */
static inline bool snan_bit_is_one(float_status *status)
{
#if defined(TARGET_MIPS)
    return status->snan_bit_is_one;
#elif defined(TARGET_HPPA) || defined(TARGET_SH4)
    return 1;
#else
    return 0;
#endif
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

#if defined(TARGET_SPARC) || defined(TARGET_M68K)
    /* !snan_bit_is_one, set all bits */
    frac = (1ULL << DECOMPOSED_BINARY_POINT) - 1;
#elif defined(TARGET_I386) || defined(TARGET_X86_64) \
    || defined(TARGET_MICROBLAZE)
    /* !snan_bit_is_one, set sign and msb */
    frac = 1ULL << (DECOMPOSED_BINARY_POINT - 1);
    sign = 1;
#elif defined(TARGET_HPPA)
    /* snan_bit_is_one, set msb-1.  */
    frac = 1ULL << (DECOMPOSED_BINARY_POINT - 2);
#elif defined(TARGET_HEXAGON)
    sign = 1;
    frac = ~0ULL;
#else
    /*
     * This case is true for Alpha, ARM, MIPS, OpenRISC, PPC, RISC-V,
     * S390, SH4, TriCore, and Xtensa.  Our other supported targets,
     * CRIS, Nios2, and Tile, do not have floating-point.
     */
    if (snan_bit_is_one(status)) {
        /* set all bits other than msb */
        frac = (1ULL << (DECOMPOSED_BINARY_POINT - 1)) - 1;
    } else {
        /* set msb */
        frac = 1ULL << (DECOMPOSED_BINARY_POINT - 1);
    }
#endif

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

    /* None of the targets that have snan_bit_is_one use floatx80.  */
    assert(!snan_bit_is_one(status));
#if defined(TARGET_M68K)
    r.low = UINT64_C(0xFFFFFFFFFFFFFFFF);
    r.high = 0x7FFF;
#else
    /* X86 */
    r.low = UINT64_C(0xC000000000000000);
    r.high = 0xFFFF;
#endif
    return r;
}

/*----------------------------------------------------------------------------
| The pattern for a default generated extended double-precision inf.
*----------------------------------------------------------------------------*/

#define floatx80_infinity_high 0x7FFF
#if defined(TARGET_M68K)
#define floatx80_infinity_low  UINT64_C(0x0000000000000000)
#else
#define floatx80_infinity_low  UINT64_C(0x8000000000000000)
#endif

const floatx80 floatx80_infinity
    = make_floatx80_init(floatx80_infinity_high, floatx80_infinity_low);

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
| Select which NaN to propagate for a two-input operation.
| IEEE754 doesn't specify all the details of this, so the
| algorithm is target-specific.
| The routine is passed various bits of information about the
| two NaNs and should return 0 to select NaN a and 1 for NaN b.
| Note that signalling NaNs are always squashed to quiet NaNs
| by the caller, by calling floatXX_silence_nan() before
| returning them.
|
| aIsLargerSignificand is only valid if both a and b are NaNs
| of some kind, and is true if a has the larger significand,
| or if both a and b have the same significand but a is
| positive but b is negative. It is only needed for the x87
| tie-break rule.
*----------------------------------------------------------------------------*/

static int pickNaN(FloatClass a_cls, FloatClass b_cls,
                   bool aIsLargerSignificand, float_status *status)
{
#if defined(TARGET_ARM) || defined(TARGET_MIPS) || defined(TARGET_HPPA)
    /* ARM mandated NaN propagation rules (see FPProcessNaNs()), take
     * the first of:
     *  1. A if it is signaling
     *  2. B if it is signaling
     *  3. A (quiet)
     *  4. B (quiet)
     * A signaling NaN is always quietened before returning it.
     */
    /* According to MIPS specifications, if one of the two operands is
     * a sNaN, a new qNaN has to be generated. This is done in
     * floatXX_silence_nan(). For qNaN inputs the specifications
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
    if (is_snan(a_cls)) {
        return 0;
    } else if (is_snan(b_cls)) {
        return 1;
    } else if (is_qnan(a_cls)) {
        return 0;
    } else {
        return 1;
    }
#elif defined(TARGET_PPC) || defined(TARGET_M68K)
    /* PowerPC propagation rules:
     *  1. A if it sNaN or qNaN
     *  2. B if it sNaN or qNaN
     * A signaling NaN is always silenced before returning it.
     */
    /* M68000 FAMILY PROGRAMMER'S REFERENCE MANUAL
     * 3.4 FLOATING-POINT INSTRUCTION DETAILS
     * If either operand, but not both operands, of an operation is a
     * nonsignaling NaN, then that NaN is returned as the result. If both
     * operands are nonsignaling NaNs, then the destination operand
     * nonsignaling NaN is returned as the result.
     * If either operand to an operation is a signaling NaN (SNaN), then the
     * SNaN bit is set in the FPSR EXC byte. If the SNaN exception enable bit
     * is set in the FPCR ENABLE byte, then the exception is taken and the
     * destination is not modified. If the SNaN exception enable bit is not
     * set, setting the SNaN bit in the operand to a one converts the SNaN to
     * a nonsignaling NaN. The operation then continues as described in the
     * preceding paragraph for nonsignaling NaNs.
     */
    if (is_nan(a_cls)) {
        return 0;
    } else {
        return 1;
    }
#elif defined(TARGET_XTENSA)
    /*
     * Xtensa has two NaN propagation modes.
     * Which one is active is controlled by float_status::use_first_nan.
     */
    if (status->use_first_nan) {
        if (is_nan(a_cls)) {
            return 0;
        } else {
            return 1;
        }
    } else {
        if (is_nan(b_cls)) {
            return 1;
        } else {
            return 0;
        }
    }
#else
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
    if (is_snan(a_cls)) {
        if (is_snan(b_cls)) {
            return aIsLargerSignificand ? 0 : 1;
        }
        return is_qnan(b_cls) ? 1 : 0;
    } else if (is_qnan(a_cls)) {
        if (is_snan(b_cls) || !is_qnan(b_cls)) {
            return 0;
        } else {
            return aIsLargerSignificand ? 0 : 1;
        }
    } else {
        return 1;
    }
#endif
}

/*----------------------------------------------------------------------------
| Select which NaN to propagate for a three-input operation.
| For the moment we assume that no CPU needs the 'larger significand'
| information.
| Return values : 0 : a; 1 : b; 2 : c; 3 : default-NaN
*----------------------------------------------------------------------------*/
static int pickNaNMulAdd(FloatClass a_cls, FloatClass b_cls, FloatClass c_cls,
                         bool infzero, float_status *status)
{
#if defined(TARGET_ARM)
    /* For ARM, the (inf,zero,qnan) case sets InvalidOp and returns
     * the default NaN
     */
    if (infzero && is_qnan(c_cls)) {
        float_raise(float_flag_invalid | float_flag_invalid_imz, status);
        return 3;
    }

    /* This looks different from the ARM ARM pseudocode, because the ARM ARM
     * puts the operands to a fused mac operation (a*b)+c in the order c,a,b.
     */
    if (is_snan(c_cls)) {
        return 2;
    } else if (is_snan(a_cls)) {
        return 0;
    } else if (is_snan(b_cls)) {
        return 1;
    } else if (is_qnan(c_cls)) {
        return 2;
    } else if (is_qnan(a_cls)) {
        return 0;
    } else {
        return 1;
    }
#elif defined(TARGET_MIPS)
    if (snan_bit_is_one(status)) {
        /*
         * For MIPS systems that conform to IEEE754-1985, the (inf,zero,nan)
         * case sets InvalidOp and returns the default NaN
         */
        if (infzero) {
            float_raise(float_flag_invalid | float_flag_invalid_imz, status);
            return 3;
        }
        /* Prefer sNaN over qNaN, in the a, b, c order. */
        if (is_snan(a_cls)) {
            return 0;
        } else if (is_snan(b_cls)) {
            return 1;
        } else if (is_snan(c_cls)) {
            return 2;
        } else if (is_qnan(a_cls)) {
            return 0;
        } else if (is_qnan(b_cls)) {
            return 1;
        } else {
            return 2;
        }
    } else {
        /*
         * For MIPS systems that conform to IEEE754-2008, the (inf,zero,nan)
         * case sets InvalidOp and returns the input value 'c'
         */
        if (infzero) {
            float_raise(float_flag_invalid | float_flag_invalid_imz, status);
            return 2;
        }
        /* Prefer sNaN over qNaN, in the c, a, b order. */
        if (is_snan(c_cls)) {
            return 2;
        } else if (is_snan(a_cls)) {
            return 0;
        } else if (is_snan(b_cls)) {
            return 1;
        } else if (is_qnan(c_cls)) {
            return 2;
        } else if (is_qnan(a_cls)) {
            return 0;
        } else {
            return 1;
        }
    }
#elif defined(TARGET_PPC)
    /* For PPC, the (inf,zero,qnan) case sets InvalidOp, but we prefer
     * to return an input NaN if we have one (ie c) rather than generating
     * a default NaN
     */
    if (infzero) {
        float_raise(float_flag_invalid | float_flag_invalid_imz, status);
        return 2;
    }

    /* If fRA is a NaN return it; otherwise if fRB is a NaN return it;
     * otherwise return fRC. Note that muladd on PPC is (fRA * fRC) + frB
     */
    if (is_nan(a_cls)) {
        return 0;
    } else if (is_nan(c_cls)) {
        return 2;
    } else {
        return 1;
    }
#elif defined(TARGET_RISCV)
    /* For RISC-V, InvalidOp is set when multiplicands are Inf and zero */
    if (infzero) {
        float_raise(float_flag_invalid | float_flag_invalid_imz, status);
    }
    return 3; /* default NaN */
#elif defined(TARGET_XTENSA)
    /*
     * For Xtensa, the (inf,zero,nan) case sets InvalidOp and returns
     * an input NaN if we have one (ie c).
     */
    if (infzero) {
        float_raise(float_flag_invalid | float_flag_invalid_imz, status);
        return 2;
    }
    if (status->use_first_nan) {
        if (is_nan(a_cls)) {
            return 0;
        } else if (is_nan(b_cls)) {
            return 1;
        } else {
            return 2;
        }
    } else {
        if (is_nan(c_cls)) {
            return 2;
        } else if (is_nan(b_cls)) {
            return 1;
        } else {
            return 0;
        }
    }
#else
    /* A default implementation: prefer a to b to c.
     * This is unlikely to actually match any real implementation.
     */
    if (is_nan(a_cls)) {
        return 0;
    } else if (is_nan(b_cls)) {
        return 1;
    } else {
        return 2;
    }
#endif
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
| Takes two extended double-precision floating-point values `a' and `b', one
| of which is a NaN, and returns the appropriate NaN result.  If either `a' or
| `b' is a signaling NaN, the invalid exception is raised.
*----------------------------------------------------------------------------*/

floatx80 propagateFloatx80NaN(floatx80 a, floatx80 b, float_status *status)
{
    bool aIsLargerSignificand;
    FloatClass a_cls, b_cls;

    /* This is not complete, but is good enough for pickNaN.  */
    a_cls = (!floatx80_is_any_nan(a)
             ? float_class_normal
             : floatx80_is_signaling_nan(a, status)
             ? float_class_snan
             : float_class_qnan);
    b_cls = (!floatx80_is_any_nan(b)
             ? float_class_normal
             : floatx80_is_signaling_nan(b, status)
             ? float_class_snan
             : float_class_qnan);

    if (is_snan(a_cls) || is_snan(b_cls)) {
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

    if (pickNaN(a_cls, b_cls, aIsLargerSignificand, status)) {
        if (is_snan(b_cls)) {
            return floatx80_silence_nan(b, status);
        }
        return b;
    } else {
        if (is_snan(a_cls)) {
            return floatx80_silence_nan(a, status);
        }
        return a;
    }
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
