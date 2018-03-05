/*
 * Ported from a work by Andreas Grabher for Previous, NeXT Computer Emulator,
 * derived from NetBSD M68040 FPSP functions,
 * derived from release 2a of the SoftFloat IEC/IEEE Floating-point Arithmetic
 * Package. Those parts of the code (and some later contributions) are
 * provided under that license, as detailed below.
 * It has subsequently been modified by contributors to the QEMU Project,
 * so some portions are provided under:
 *  the SoftFloat-2a license
 *  the BSD license
 *  GPL-v2-or-later
 *
 * Any future contributions to this file will be taken to be licensed under
 * the Softfloat-2a license unless specifically indicated otherwise.
 */

/* Portions of this work are licensed under the terms of the GNU GPL,
 * version 2 or later. See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "softfloat.h"
#include "fpu/softfloat-macros.h"
#include "softfloat_fpsp_tables.h"

static floatx80 propagateFloatx80NaNOneArg(floatx80 a, float_status *status)
{
    if (floatx80_is_signaling_nan(a, status)) {
        float_raise(float_flag_invalid, status);
    }

    if (status->default_nan_mode) {
        return floatx80_default_nan(status);
    }

    return floatx80_maybe_silence_nan(a, status);
}

/*----------------------------------------------------------------------------
 | Returns the modulo remainder of the extended double-precision floating-point
 | value `a' with respect to the corresponding value `b'.
 *----------------------------------------------------------------------------*/

floatx80 floatx80_mod(floatx80 a, floatx80 b, float_status *status)
{
    flag aSign, zSign;
    int32_t aExp, bExp, expDiff;
    uint64_t aSig0, aSig1, bSig;
    uint64_t qTemp, term0, term1;

    aSig0 = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    bSig = extractFloatx80Frac(b);
    bExp = extractFloatx80Exp(b);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig0 << 1)
            || ((bExp == 0x7FFF) && (uint64_t) (bSig << 1))) {
            return propagateFloatx80NaN(a, b, status);
        }
        goto invalid;
    }
    if (bExp == 0x7FFF) {
        if ((uint64_t) (bSig << 1)) {
            return propagateFloatx80NaN(a, b, status);
        }
        return a;
    }
    if (bExp == 0) {
        if (bSig == 0) {
        invalid:
            float_raise(float_flag_invalid, status);
            return floatx80_default_nan(status);
        }
        normalizeFloatx80Subnormal(bSig, &bExp, &bSig);
    }
    if (aExp == 0) {
        if ((uint64_t) (aSig0 << 1) == 0) {
            return a;
        }
        normalizeFloatx80Subnormal(aSig0, &aExp, &aSig0);
    }
    bSig |= LIT64(0x8000000000000000);
    zSign = aSign;
    expDiff = aExp - bExp;
    aSig1 = 0;
    if (expDiff < 0) {
        return a;
    }
    qTemp = (bSig <= aSig0);
    if (qTemp) {
        aSig0 -= bSig;
    }
    expDiff -= 64;
    while (0 < expDiff) {
        qTemp = estimateDiv128To64(aSig0, aSig1, bSig);
        qTemp = (2 < qTemp) ? qTemp - 2 : 0;
        mul64To128(bSig, qTemp, &term0, &term1);
        sub128(aSig0, aSig1, term0, term1, &aSig0, &aSig1);
        shortShift128Left(aSig0, aSig1, 62, &aSig0, &aSig1);
    }
    expDiff += 64;
    if (0 < expDiff) {
        qTemp = estimateDiv128To64(aSig0, aSig1, bSig);
        qTemp = (2 < qTemp) ? qTemp - 2 : 0;
        qTemp >>= 64 - expDiff;
        mul64To128(bSig, qTemp << (64 - expDiff), &term0, &term1);
        sub128(aSig0, aSig1, term0, term1, &aSig0, &aSig1);
        shortShift128Left(0, bSig, 64 - expDiff, &term0, &term1);
        while (le128(term0, term1, aSig0, aSig1)) {
            ++qTemp;
            sub128(aSig0, aSig1, term0, term1, &aSig0, &aSig1);
        }
    }
    return
        normalizeRoundAndPackFloatx80(
            80, zSign, bExp + expDiff, aSig0, aSig1, status);
}

/*----------------------------------------------------------------------------
 | Returns the mantissa of the extended double-precision floating-point
 | value `a'.
 *----------------------------------------------------------------------------*/

floatx80 floatx80_getman(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a , status);
        }
        float_raise(float_flag_invalid , status);
        return floatx80_default_nan(status);
    }

    if (aExp == 0) {
        if (aSig == 0) {
            return packFloatx80(aSign, 0, 0);
        }
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }

    return roundAndPackFloatx80(status->floatx80_rounding_precision, aSign,
                                0x3FFF, aSig, 0, status);
}

/*----------------------------------------------------------------------------
 | Returns the exponent of the extended double-precision floating-point
 | value `a' as an extended double-precision value.
 *----------------------------------------------------------------------------*/

floatx80 floatx80_getexp(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a , status);
        }
        float_raise(float_flag_invalid , status);
        return floatx80_default_nan(status);
    }

    if (aExp == 0) {
        if (aSig == 0) {
            return packFloatx80(aSign, 0, 0);
        }
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }

    return int32_to_floatx80(aExp - 0x3FFF, status);
}

/*----------------------------------------------------------------------------
 | Scales extended double-precision floating-point value in operand `a' by
 | value `b'. The function truncates the value in the second operand 'b' to
 | an integral value and adds that value to the exponent of the operand 'a'.
 | The operation performed according to the IEC/IEEE Standard for Binary
 | Floating-Point Arithmetic.
 *----------------------------------------------------------------------------*/

floatx80 floatx80_scale(floatx80 a, floatx80 b, float_status *status)
{
    flag aSign, bSign;
    int32_t aExp, bExp, shiftCount;
    uint64_t aSig, bSig;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    bSig = extractFloatx80Frac(b);
    bExp = extractFloatx80Exp(b);
    bSign = extractFloatx80Sign(b);

    if (bExp == 0x7FFF) {
        if ((uint64_t) (bSig << 1) ||
            ((aExp == 0x7FFF) && (uint64_t) (aSig << 1))) {
            return propagateFloatx80NaN(a, b, status);
        }
        float_raise(float_flag_invalid , status);
        return floatx80_default_nan(status);
    }
    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaN(a, b, status);
        }
        return packFloatx80(aSign, floatx80_infinity.high,
                            floatx80_infinity.low);
    }
    if (aExp == 0) {
        if (aSig == 0) {
            return packFloatx80(aSign, 0, 0);
        }
        if (bExp < 0x3FFF) {
            return a;
        }
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }

    if (bExp < 0x3FFF) {
        return a;
    }

    if (0x400F < bExp) {
        aExp = bSign ? -0x6001 : 0xE000;
        return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                    aSign, aExp, aSig, 0, status);
    }

    shiftCount = 0x403E - bExp;
    bSig >>= shiftCount;
    aExp = bSign ? (aExp - bSig) : (aExp + bSig);

    return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                aSign, aExp, aSig, 0, status);
}

floatx80 floatx80_move(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t)(aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        return a;
    }
    if (aExp == 0) {
        if (aSig == 0) {
            return a;
        }
        normalizeRoundAndPackFloatx80(status->floatx80_rounding_precision,
                                      aSign, aExp, aSig, 0, status);
    }
    return roundAndPackFloatx80(status->floatx80_rounding_precision, aSign,
                                aExp, aSig, 0, status);
}

/*----------------------------------------------------------------------------
| Algorithms for transcendental functions supported by MC68881 and MC68882
| mathematical coprocessors. The functions are derived from FPSP library.
*----------------------------------------------------------------------------*/

#define one_exp     0x3FFF
#define one_sig     LIT64(0x8000000000000000)

/*----------------------------------------------------------------------------
 | Function for compactifying extended double-precision floating point values.
 *----------------------------------------------------------------------------*/

static int32_t floatx80_make_compact(int32_t aExp, uint64_t aSig)
{
    return (aExp << 16) | (aSig >> 48);
}

/*----------------------------------------------------------------------------
 | Log base e of x plus 1
 *----------------------------------------------------------------------------*/

floatx80 floatx80_lognp1(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig, fSig;

    int8_t user_rnd_mode, user_rnd_prec;

    int32_t compact, j, k;
    floatx80 fp0, fp1, fp2, fp3, f, logof2, klog2, saveu;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            propagateFloatx80NaNOneArg(a, status);
        }
        if (aSign) {
            float_raise(float_flag_invalid, status);
            return floatx80_default_nan(status);
        }
        return packFloatx80(0, floatx80_infinity.high, floatx80_infinity.low);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(aSign, 0, 0);
    }

    if (aSign && aExp >= one_exp) {
        if (aExp == one_exp && aSig == one_sig) {
            float_raise(float_flag_divbyzero, status);
            packFloatx80(aSign, floatx80_infinity.high, floatx80_infinity.low);
        }
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }

    if (aExp < 0x3f99 || (aExp == 0x3f99 && aSig == one_sig)) {
        /* <= min threshold */
        float_raise(float_flag_inexact, status);
        return floatx80_move(a, status);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    compact = floatx80_make_compact(aExp, aSig);

    fp0 = a; /* Z */
    fp1 = a;

    fp0 = floatx80_add(fp0, float32_to_floatx80(make_float32(0x3F800000),
                       status), status); /* X = (1+Z) */

    aExp = extractFloatx80Exp(fp0);
    aSig = extractFloatx80Frac(fp0);

    compact = floatx80_make_compact(aExp, aSig);

    if (compact < 0x3FFE8000 || compact > 0x3FFFC000) {
        /* |X| < 1/2 or |X| > 3/2 */
        k = aExp - 0x3FFF;
        fp1 = int32_to_floatx80(k, status);

        fSig = (aSig & LIT64(0xFE00000000000000)) | LIT64(0x0100000000000000);
        j = (fSig >> 56) & 0x7E; /* DISPLACEMENT FOR 1/F */

        f = packFloatx80(0, 0x3FFF, fSig); /* F */
        fp0 = packFloatx80(0, 0x3FFF, aSig); /* Y */

        fp0 = floatx80_sub(fp0, f, status); /* Y-F */

    lp1cont1:
        /* LP1CONT1 */
        fp0 = floatx80_mul(fp0, log_tbl[j], status); /* FP0 IS U = (Y-F)/F */
        logof2 = packFloatx80(0, 0x3FFE, LIT64(0xB17217F7D1CF79AC));
        klog2 = floatx80_mul(fp1, logof2, status); /* FP1 IS K*LOG2 */
        fp2 = floatx80_mul(fp0, fp0, status); /* FP2 IS V=U*U */

        fp3 = fp2;
        fp1 = fp2;

        fp1 = floatx80_mul(fp1, float64_to_floatx80(
                           make_float64(0x3FC2499AB5E4040B), status),
                           status); /* V*A6 */
        fp2 = floatx80_mul(fp2, float64_to_floatx80(
                           make_float64(0xBFC555B5848CB7DB), status),
                           status); /* V*A5 */
        fp1 = floatx80_add(fp1, float64_to_floatx80(
                           make_float64(0x3FC99999987D8730), status),
                           status); /* A4+V*A6 */
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0xBFCFFFFFFF6F7E97), status),
                           status); /* A3+V*A5 */
        fp1 = floatx80_mul(fp1, fp3, status); /* V*(A4+V*A6) */
        fp2 = floatx80_mul(fp2, fp3, status); /* V*(A3+V*A5) */
        fp1 = floatx80_add(fp1, float64_to_floatx80(
                           make_float64(0x3FD55555555555A4), status),
                           status); /* A2+V*(A4+V*A6) */
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0xBFE0000000000008), status),
                           status); /* A1+V*(A3+V*A5) */
        fp1 = floatx80_mul(fp1, fp3, status); /* V*(A2+V*(A4+V*A6)) */
        fp2 = floatx80_mul(fp2, fp3, status); /* V*(A1+V*(A3+V*A5)) */
        fp1 = floatx80_mul(fp1, fp0, status); /* U*V*(A2+V*(A4+V*A6)) */
        fp0 = floatx80_add(fp0, fp2, status); /* U+V*(A1+V*(A3+V*A5)) */

        fp1 = floatx80_add(fp1, log_tbl[j + 1],
                           status); /* LOG(F)+U*V*(A2+V*(A4+V*A6)) */
        fp0 = floatx80_add(fp0, fp1, status); /* FP0 IS LOG(F) + LOG(1+U) */

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_add(fp0, klog2, status);

        float_raise(float_flag_inexact, status);

        return a;
    } else if (compact < 0x3FFEF07D || compact > 0x3FFF8841) {
        /* |X| < 1/16 or |X| > -1/16 */
        /* LP1CARE */
        fSig = (aSig & LIT64(0xFE00000000000000)) | LIT64(0x0100000000000000);
        f = packFloatx80(0, 0x3FFF, fSig); /* F */
        j = (fSig >> 56) & 0x7E; /* DISPLACEMENT FOR 1/F */

        if (compact >= 0x3FFF8000) { /* 1+Z >= 1 */
            /* KISZERO */
            fp0 = floatx80_sub(float32_to_floatx80(make_float32(0x3F800000),
                               status), f, status); /* 1-F */
            fp0 = floatx80_add(fp0, fp1, status); /* FP0 IS Y-F = (1-F)+Z */
            fp1 = packFloatx80(0, 0, 0); /* K = 0 */
        } else {
            /* KISNEG */
            fp0 = floatx80_sub(float32_to_floatx80(make_float32(0x40000000),
                               status), f, status); /* 2-F */
            fp1 = floatx80_add(fp1, fp1, status); /* 2Z */
            fp0 = floatx80_add(fp0, fp1, status); /* FP0 IS Y-F = (2-F)+2Z */
            fp1 = packFloatx80(1, one_exp, one_sig); /* K = -1 */
        }
        goto lp1cont1;
    } else {
        /* LP1ONE16 */
        fp1 = floatx80_add(fp1, fp1, status); /* FP1 IS 2Z */
        fp0 = floatx80_add(fp0, float32_to_floatx80(make_float32(0x3F800000),
                           status), status); /* FP0 IS 1+X */

        /* LP1CONT2 */
        fp1 = floatx80_div(fp1, fp0, status); /* U */
        saveu = fp1;
        fp0 = floatx80_mul(fp1, fp1, status); /* FP0 IS V = U*U */
        fp1 = floatx80_mul(fp0, fp0, status); /* FP1 IS W = V*V */

        fp3 = float64_to_floatx80(make_float64(0x3F175496ADD7DAD6),
                                  status); /* B5 */
        fp2 = float64_to_floatx80(make_float64(0x3F3C71C2FE80C7E0),
                                  status); /* B4 */
        fp3 = floatx80_mul(fp3, fp1, status); /* W*B5 */
        fp2 = floatx80_mul(fp2, fp1, status); /* W*B4 */
        fp3 = floatx80_add(fp3, float64_to_floatx80(
                           make_float64(0x3F624924928BCCFF), status),
                           status); /* B3+W*B5 */
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0x3F899999999995EC), status),
                           status); /* B2+W*B4 */
        fp1 = floatx80_mul(fp1, fp3, status); /* W*(B3+W*B5) */
        fp2 = floatx80_mul(fp2, fp0, status); /* V*(B2+W*B4) */
        fp1 = floatx80_add(fp1, float64_to_floatx80(
                           make_float64(0x3FB5555555555555), status),
                           status); /* B1+W*(B3+W*B5) */

        fp0 = floatx80_mul(fp0, saveu, status); /* FP0 IS U*V */
        fp1 = floatx80_add(fp1, fp2,
                           status); /* B1+W*(B3+W*B5) + V*(B2+W*B4) */
        fp0 = floatx80_mul(fp0, fp1,
                           status); /* U*V*([B1+W*(B3+W*B5)] + [V*(B2+W*B4)]) */

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_add(fp0, saveu, status);

        /*if (!floatx80_is_zero(a)) { */
            float_raise(float_flag_inexact, status);
        /*} */

        return a;
    }
}

/*----------------------------------------------------------------------------
 | Log base e
 *----------------------------------------------------------------------------*/

floatx80 floatx80_logn(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig, fSig;

    int8_t user_rnd_mode, user_rnd_prec;

    int32_t compact, j, k, adjk;
    floatx80 fp0, fp1, fp2, fp3, f, logof2, klog2, saveu;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            propagateFloatx80NaNOneArg(a, status);
        }
        if (aSign == 0) {
            return packFloatx80(0, floatx80_infinity.high,
                                floatx80_infinity.low);
        }
    }

    adjk = 0;

    if (aExp == 0) {
        if (aSig == 0) { /* zero */
            float_raise(float_flag_divbyzero, status);
            return packFloatx80(1, floatx80_infinity.high,
                                floatx80_infinity.low);
        }
        if ((aSig & one_sig) == 0) { /* denormal */
            normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
            adjk = -100;
            aExp += 100;
            a = packFloatx80(aSign, aExp, aSig);
        }
    }

    if (aSign) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    compact = floatx80_make_compact(aExp, aSig);

    if (compact < 0x3FFEF07D || compact > 0x3FFF8841) {
        /* |X| < 15/16 or |X| > 17/16 */
        k = aExp - 0x3FFF;
        k += adjk;
        fp1 = int32_to_floatx80(k, status);

        fSig = (aSig & LIT64(0xFE00000000000000)) | LIT64(0x0100000000000000);
        j = (fSig >> 56) & 0x7E; /* DISPLACEMENT FOR 1/F */

        f = packFloatx80(0, 0x3FFF, fSig); /* F */
        fp0 = packFloatx80(0, 0x3FFF, aSig); /* Y */

        fp0 = floatx80_sub(fp0, f, status); /* Y-F */

        /* LP1CONT1 */
        fp0 = floatx80_mul(fp0, log_tbl[j], status); /* FP0 IS U = (Y-F)/F */
        logof2 = packFloatx80(0, 0x3FFE, LIT64(0xB17217F7D1CF79AC));
        klog2 = floatx80_mul(fp1, logof2, status); /* FP1 IS K*LOG2 */
        fp2 = floatx80_mul(fp0, fp0, status); /* FP2 IS V=U*U */

        fp3 = fp2;
        fp1 = fp2;

        fp1 = floatx80_mul(fp1, float64_to_floatx80(
                           make_float64(0x3FC2499AB5E4040B), status),
                           status); /* V*A6 */
        fp2 = floatx80_mul(fp2, float64_to_floatx80(
                           make_float64(0xBFC555B5848CB7DB), status),
                           status); /* V*A5 */
        fp1 = floatx80_add(fp1, float64_to_floatx80(
                           make_float64(0x3FC99999987D8730), status),
                           status); /* A4+V*A6 */
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0xBFCFFFFFFF6F7E97), status),
                           status); /* A3+V*A5 */
        fp1 = floatx80_mul(fp1, fp3, status); /* V*(A4+V*A6) */
        fp2 = floatx80_mul(fp2, fp3, status); /* V*(A3+V*A5) */
        fp1 = floatx80_add(fp1, float64_to_floatx80(
                           make_float64(0x3FD55555555555A4), status),
                           status); /* A2+V*(A4+V*A6) */
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0xBFE0000000000008), status),
                           status); /* A1+V*(A3+V*A5) */
        fp1 = floatx80_mul(fp1, fp3, status); /* V*(A2+V*(A4+V*A6)) */
        fp2 = floatx80_mul(fp2, fp3, status); /* V*(A1+V*(A3+V*A5)) */
        fp1 = floatx80_mul(fp1, fp0, status); /* U*V*(A2+V*(A4+V*A6)) */
        fp0 = floatx80_add(fp0, fp2, status); /* U+V*(A1+V*(A3+V*A5)) */

        fp1 = floatx80_add(fp1, log_tbl[j + 1],
                           status); /* LOG(F)+U*V*(A2+V*(A4+V*A6)) */
        fp0 = floatx80_add(fp0, fp1, status); /* FP0 IS LOG(F) + LOG(1+U) */

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_add(fp0, klog2, status);

        float_raise(float_flag_inexact, status);

        return a;
    } else { /* |X-1| >= 1/16 */
        fp0 = a;
        fp1 = a;
        fp1 = floatx80_sub(fp1, float32_to_floatx80(make_float32(0x3F800000),
                           status), status); /* FP1 IS X-1 */
        fp0 = floatx80_add(fp0, float32_to_floatx80(make_float32(0x3F800000),
                           status), status); /* FP0 IS X+1 */
        fp1 = floatx80_add(fp1, fp1, status); /* FP1 IS 2(X-1) */

        /* LP1CONT2 */
        fp1 = floatx80_div(fp1, fp0, status); /* U */
        saveu = fp1;
        fp0 = floatx80_mul(fp1, fp1, status); /* FP0 IS V = U*U */
        fp1 = floatx80_mul(fp0, fp0, status); /* FP1 IS W = V*V */

        fp3 = float64_to_floatx80(make_float64(0x3F175496ADD7DAD6),
                                  status); /* B5 */
        fp2 = float64_to_floatx80(make_float64(0x3F3C71C2FE80C7E0),
                                  status); /* B4 */
        fp3 = floatx80_mul(fp3, fp1, status); /* W*B5 */
        fp2 = floatx80_mul(fp2, fp1, status); /* W*B4 */
        fp3 = floatx80_add(fp3, float64_to_floatx80(
                           make_float64(0x3F624924928BCCFF), status),
                           status); /* B3+W*B5 */
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0x3F899999999995EC), status),
                           status); /* B2+W*B4 */
        fp1 = floatx80_mul(fp1, fp3, status); /* W*(B3+W*B5) */
        fp2 = floatx80_mul(fp2, fp0, status); /* V*(B2+W*B4) */
        fp1 = floatx80_add(fp1, float64_to_floatx80(
                           make_float64(0x3FB5555555555555), status),
                           status); /* B1+W*(B3+W*B5) */

        fp0 = floatx80_mul(fp0, saveu, status); /* FP0 IS U*V */
        fp1 = floatx80_add(fp1, fp2, status); /* B1+W*(B3+W*B5) + V*(B2+W*B4) */
        fp0 = floatx80_mul(fp0, fp1,
                           status); /* U*V*([B1+W*(B3+W*B5)] + [V*(B2+W*B4)]) */

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_add(fp0, saveu, status);

        /*if (!floatx80_is_zero(a)) { */
            float_raise(float_flag_inexact, status);
        /*} */

        return a;
    }
}

/*----------------------------------------------------------------------------
 | Log base 10
 *----------------------------------------------------------------------------*/

floatx80 floatx80_log10(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    int8_t user_rnd_mode, user_rnd_prec;

    floatx80 fp0, fp1;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            propagateFloatx80NaNOneArg(a, status);
        }
        if (aSign == 0) {
            return packFloatx80(0, floatx80_infinity.high,
                                floatx80_infinity.low);
        }
    }

    if (aExp == 0 && aSig == 0) {
        float_raise(float_flag_divbyzero, status);
        return packFloatx80(1, floatx80_infinity.high,
                            floatx80_infinity.low);
    }

    if (aSign) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    fp0 = floatx80_logn(a, status);
    fp1 = packFloatx80(0, 0x3FFD, LIT64(0xDE5BD8A937287195)); /* INV_L10 */

    status->float_rounding_mode = user_rnd_mode;
    status->floatx80_rounding_precision = user_rnd_prec;

    a = floatx80_mul(fp0, fp1, status); /* LOGN(X)*INV_L10 */

    float_raise(float_flag_inexact, status);

    return a;
}

/*----------------------------------------------------------------------------
 | Log base 2
 *----------------------------------------------------------------------------*/

floatx80 floatx80_log2(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    int8_t user_rnd_mode, user_rnd_prec;

    floatx80 fp0, fp1;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            propagateFloatx80NaNOneArg(a, status);
        }
        if (aSign == 0) {
            return packFloatx80(0, floatx80_infinity.high,
                                floatx80_infinity.low);
        }
    }

    if (aExp == 0) {
        if (aSig == 0) {
            float_raise(float_flag_divbyzero, status);
            return packFloatx80(1, floatx80_infinity.high,
                                floatx80_infinity.low);
        }
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }

    if (aSign) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    if (aSig == one_sig) { /* X is 2^k */
        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = int32_to_floatx80(aExp - 0x3FFF, status);
    } else {
        fp0 = floatx80_logn(a, status);
        fp1 = packFloatx80(0, 0x3FFF, LIT64(0xB8AA3B295C17F0BC)); /* INV_L2 */

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_mul(fp0, fp1, status); /* LOGN(X)*INV_L2 */
    }

    float_raise(float_flag_inexact, status);

    return a;
}

/*----------------------------------------------------------------------------
 | e to x
 *----------------------------------------------------------------------------*/

floatx80 floatx80_etox(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    int8_t user_rnd_mode, user_rnd_prec;

    int32_t compact, n, j, k, m, m1;
    floatx80 fp0, fp1, fp2, fp3, l2, scale, adjscale;
    flag adjflag;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        if (aSign) {
            return packFloatx80(0, 0, 0);
        }
        return packFloatx80(0, floatx80_infinity.high,
                            floatx80_infinity.low);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(0, one_exp, one_sig);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    adjflag = 0;

    if (aExp >= 0x3FBE) { /* |X| >= 2^(-65) */
        compact = floatx80_make_compact(aExp, aSig);

        if (compact < 0x400CB167) { /* |X| < 16380 log2 */
            fp0 = a;
            fp1 = a;
            fp0 = floatx80_mul(fp0, float32_to_floatx80(
                               make_float32(0x42B8AA3B), status),
                               status); /* 64/log2 * X */
            adjflag = 0;
            n = floatx80_to_int32(fp0, status); /* int(64/log2*X) */
            fp0 = int32_to_floatx80(n, status);

            j = n & 0x3F; /* J = N mod 64 */
            m = n / 64; /* NOTE: this is really arithmetic right shift by 6 */
            if (n < 0 && j) {
                /* arithmetic right shift is division and
                 * round towards minus infinity
                 */
                m--;
            }
            m += 0x3FFF; /* biased exponent of 2^(M) */

        expcont1:
            fp2 = fp0; /* N */
            fp0 = floatx80_mul(fp0, float32_to_floatx80(
                               make_float32(0xBC317218), status),
                               status); /* N * L1, L1 = lead(-log2/64) */
            l2 = packFloatx80(0, 0x3FDC, LIT64(0x82E308654361C4C6));
            fp2 = floatx80_mul(fp2, l2, status); /* N * L2, L1+L2 = -log2/64 */
            fp0 = floatx80_add(fp0, fp1, status); /* X + N*L1 */
            fp0 = floatx80_add(fp0, fp2, status); /* R */

            fp1 = floatx80_mul(fp0, fp0, status); /* S = R*R */
            fp2 = float32_to_floatx80(make_float32(0x3AB60B70),
                                      status); /* A5 */
            fp2 = floatx80_mul(fp2, fp1, status); /* fp2 is S*A5 */
            fp3 = floatx80_mul(float32_to_floatx80(make_float32(0x3C088895),
                               status), fp1,
                               status); /* fp3 is S*A4 */
            fp2 = floatx80_add(fp2, float64_to_floatx80(make_float64(
                               0x3FA5555555554431), status),
                               status); /* fp2 is A3+S*A5 */
            fp3 = floatx80_add(fp3, float64_to_floatx80(make_float64(
                               0x3FC5555555554018), status),
                               status); /* fp3 is A2+S*A4 */
            fp2 = floatx80_mul(fp2, fp1, status); /* fp2 is S*(A3+S*A5) */
            fp3 = floatx80_mul(fp3, fp1, status); /* fp3 is S*(A2+S*A4) */
            fp2 = floatx80_add(fp2, float32_to_floatx80(
                               make_float32(0x3F000000), status),
                               status); /* fp2 is A1+S*(A3+S*A5) */
            fp3 = floatx80_mul(fp3, fp0, status); /* fp3 IS R*S*(A2+S*A4) */
            fp2 = floatx80_mul(fp2, fp1,
                               status); /* fp2 IS S*(A1+S*(A3+S*A5)) */
            fp0 = floatx80_add(fp0, fp3, status); /* fp0 IS R+R*S*(A2+S*A4) */
            fp0 = floatx80_add(fp0, fp2, status); /* fp0 IS EXP(R) - 1 */

            fp1 = exp_tbl[j];
            fp0 = floatx80_mul(fp0, fp1, status); /* 2^(J/64)*(Exp(R)-1) */
            fp0 = floatx80_add(fp0, float32_to_floatx80(exp_tbl2[j], status),
                               status); /* accurate 2^(J/64) */
            fp0 = floatx80_add(fp0, fp1,
                               status); /* 2^(J/64) + 2^(J/64)*(Exp(R)-1) */

            scale = packFloatx80(0, m, one_sig);
            if (adjflag) {
                adjscale = packFloatx80(0, m1, one_sig);
                fp0 = floatx80_mul(fp0, adjscale, status);
            }

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_mul(fp0, scale, status);

            float_raise(float_flag_inexact, status);

            return a;
        } else { /* |X| >= 16380 log2 */
            if (compact > 0x400CB27C) { /* |X| >= 16480 log2 */
                status->float_rounding_mode = user_rnd_mode;
                status->floatx80_rounding_precision = user_rnd_prec;
                if (aSign) {
                    a = roundAndPackFloatx80(
                                           status->floatx80_rounding_precision,
                                           0, -0x1000, aSig, 0, status);
                } else {
                    a = roundAndPackFloatx80(
                                           status->floatx80_rounding_precision,
                                           0, 0x8000, aSig, 0, status);
                }
                float_raise(float_flag_inexact, status);

                return a;
            } else {
                fp0 = a;
                fp1 = a;
                fp0 = floatx80_mul(fp0, float32_to_floatx80(
                                   make_float32(0x42B8AA3B), status),
                                   status); /* 64/log2 * X */
                adjflag = 1;
                n = floatx80_to_int32(fp0, status); /* int(64/log2*X) */
                fp0 = int32_to_floatx80(n, status);

                j = n & 0x3F; /* J = N mod 64 */
                /* NOTE: this is really arithmetic right shift by 6 */
                k = n / 64;
                if (n < 0 && j) {
                    /* arithmetic right shift is division and
                     * round towards minus infinity
                     */
                    k--;
                }
                /* NOTE: this is really arithmetic right shift by 1 */
                m1 = k / 2;
                if (k < 0 && (k & 1)) {
                    /* arithmetic right shift is division and
                     * round towards minus infinity
                     */
                    m1--;
                }
                m = k - m1;
                m1 += 0x3FFF; /* biased exponent of 2^(M1) */
                m += 0x3FFF; /* biased exponent of 2^(M) */

                goto expcont1;
            }
        }
    } else { /* |X| < 2^(-65) */
        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_add(a, float32_to_floatx80(make_float32(0x3F800000),
                         status), status); /* 1 + X */

        float_raise(float_flag_inexact, status);

        return a;
    }
}

/*----------------------------------------------------------------------------
 | 2 to x
 *----------------------------------------------------------------------------*/

floatx80 floatx80_twotox(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    int8_t user_rnd_mode, user_rnd_prec;

    int32_t compact, n, j, l, m, m1;
    floatx80 fp0, fp1, fp2, fp3, adjfact, fact1, fact2;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        if (aSign) {
            return packFloatx80(0, 0, 0);
        }
        return packFloatx80(0, floatx80_infinity.high,
                            floatx80_infinity.low);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(0, one_exp, one_sig);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    fp0 = a;

    compact = floatx80_make_compact(aExp, aSig);

    if (compact < 0x3FB98000 || compact > 0x400D80C0) {
        /* |X| > 16480 or |X| < 2^(-70) */
        if (compact > 0x3FFF8000) { /* |X| > 16480 */
            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            if (aSign) {
                return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                            0, -0x1000, aSig, 0, status);
            } else {
                return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                            0, 0x8000, aSig, 0, status);
            }
        } else { /* |X| < 2^(-70) */
            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_add(fp0, float32_to_floatx80(
                             make_float32(0x3F800000), status),
                             status); /* 1 + X */

            float_raise(float_flag_inexact, status);

            return a;
        }
    } else { /* 2^(-70) <= |X| <= 16480 */
        fp1 = fp0; /* X */
        fp1 = floatx80_mul(fp1, float32_to_floatx80(
                           make_float32(0x42800000), status),
                           status); /* X * 64 */
        n = floatx80_to_int32(fp1, status);
        fp1 = int32_to_floatx80(n, status);
        j = n & 0x3F;
        l = n / 64; /* NOTE: this is really arithmetic right shift by 6 */
        if (n < 0 && j) {
            /* arithmetic right shift is division and
             * round towards minus infinity
             */
            l--;
        }
        m = l / 2; /* NOTE: this is really arithmetic right shift by 1 */
        if (l < 0 && (l & 1)) {
            /* arithmetic right shift is division and
             * round towards minus infinity
             */
            m--;
        }
        m1 = l - m;
        m1 += 0x3FFF; /* ADJFACT IS 2^(M') */

        adjfact = packFloatx80(0, m1, one_sig);
        fact1 = exp2_tbl[j];
        fact1.high += m;
        fact2.high = exp2_tbl2[j] >> 16;
        fact2.high += m;
        fact2.low = (uint64_t)(exp2_tbl2[j] & 0xFFFF);
        fact2.low <<= 48;

        fp1 = floatx80_mul(fp1, float32_to_floatx80(
                           make_float32(0x3C800000), status),
                           status); /* (1/64)*N */
        fp0 = floatx80_sub(fp0, fp1, status); /* X - (1/64)*INT(64 X) */
        fp2 = packFloatx80(0, 0x3FFE, LIT64(0xB17217F7D1CF79AC)); /* LOG2 */
        fp0 = floatx80_mul(fp0, fp2, status); /* R */

        /* EXPR */
        fp1 = floatx80_mul(fp0, fp0, status); /* S = R*R */
        fp2 = float64_to_floatx80(make_float64(0x3F56C16D6F7BD0B2),
                                  status); /* A5 */
        fp3 = float64_to_floatx80(make_float64(0x3F811112302C712C),
                                  status); /* A4 */
        fp2 = floatx80_mul(fp2, fp1, status); /* S*A5 */
        fp3 = floatx80_mul(fp3, fp1, status); /* S*A4 */
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0x3FA5555555554CC1), status),
                           status); /* A3+S*A5 */
        fp3 = floatx80_add(fp3, float64_to_floatx80(
                           make_float64(0x3FC5555555554A54), status),
                           status); /* A2+S*A4 */
        fp2 = floatx80_mul(fp2, fp1, status); /* S*(A3+S*A5) */
        fp3 = floatx80_mul(fp3, fp1, status); /* S*(A2+S*A4) */
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0x3FE0000000000000), status),
                           status); /* A1+S*(A3+S*A5) */
        fp3 = floatx80_mul(fp3, fp0, status); /* R*S*(A2+S*A4) */

        fp2 = floatx80_mul(fp2, fp1, status); /* S*(A1+S*(A3+S*A5)) */
        fp0 = floatx80_add(fp0, fp3, status); /* R+R*S*(A2+S*A4) */
        fp0 = floatx80_add(fp0, fp2, status); /* EXP(R) - 1 */

        fp0 = floatx80_mul(fp0, fact1, status);
        fp0 = floatx80_add(fp0, fact2, status);
        fp0 = floatx80_add(fp0, fact1, status);

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_mul(fp0, adjfact, status);

        float_raise(float_flag_inexact, status);

        return a;
    }
}

/*----------------------------------------------------------------------------
 | 10 to x
 *----------------------------------------------------------------------------*/

floatx80 floatx80_tentox(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    int8_t user_rnd_mode, user_rnd_prec;

    int32_t compact, n, j, l, m, m1;
    floatx80 fp0, fp1, fp2, fp3, adjfact, fact1, fact2;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        if (aSign) {
            return packFloatx80(0, 0, 0);
        }
        return packFloatx80(0, floatx80_infinity.high,
                            floatx80_infinity.low);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(0, one_exp, one_sig);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    fp0 = a;

    compact = floatx80_make_compact(aExp, aSig);

    if (compact < 0x3FB98000 || compact > 0x400B9B07) {
        /* |X| > 16480 LOG2/LOG10 or |X| < 2^(-70) */
        if (compact > 0x3FFF8000) { /* |X| > 16480 */
            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            if (aSign) {
                return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                            0, -0x1000, aSig, 0, status);
            } else {
                return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                            0, 0x8000, aSig, 0, status);
            }
        } else { /* |X| < 2^(-70) */
            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_add(fp0, float32_to_floatx80(
                             make_float32(0x3F800000), status),
                             status); /* 1 + X */

            float_raise(float_flag_inexact, status);

            return a;
        }
    } else { /* 2^(-70) <= |X| <= 16480 LOG 2 / LOG 10 */
        fp1 = fp0; /* X */
        fp1 = floatx80_mul(fp1, float64_to_floatx80(
                           make_float64(0x406A934F0979A371),
                           status), status); /* X*64*LOG10/LOG2 */
        n = floatx80_to_int32(fp1, status); /* N=INT(X*64*LOG10/LOG2) */
        fp1 = int32_to_floatx80(n, status);

        j = n & 0x3F;
        l = n / 64; /* NOTE: this is really arithmetic right shift by 6 */
        if (n < 0 && j) {
            /* arithmetic right shift is division and
             * round towards minus infinity
             */
            l--;
        }
        m = l / 2; /* NOTE: this is really arithmetic right shift by 1 */
        if (l < 0 && (l & 1)) {
            /* arithmetic right shift is division and
             * round towards minus infinity
             */
            m--;
        }
        m1 = l - m;
        m1 += 0x3FFF; /* ADJFACT IS 2^(M') */

        adjfact = packFloatx80(0, m1, one_sig);
        fact1 = exp2_tbl[j];
        fact1.high += m;
        fact2.high = exp2_tbl2[j] >> 16;
        fact2.high += m;
        fact2.low = (uint64_t)(exp2_tbl2[j] & 0xFFFF);
        fact2.low <<= 48;

        fp2 = fp1; /* N */
        fp1 = floatx80_mul(fp1, float64_to_floatx80(
                           make_float64(0x3F734413509F8000), status),
                           status); /* N*(LOG2/64LOG10)_LEAD */
        fp3 = packFloatx80(1, 0x3FCD, LIT64(0xC0219DC1DA994FD2));
        fp2 = floatx80_mul(fp2, fp3, status); /* N*(LOG2/64LOG10)_TRAIL */
        fp0 = floatx80_sub(fp0, fp1, status); /* X - N L_LEAD */
        fp0 = floatx80_sub(fp0, fp2, status); /* X - N L_TRAIL */
        fp2 = packFloatx80(0, 0x4000, LIT64(0x935D8DDDAAA8AC17)); /* LOG10 */
        fp0 = floatx80_mul(fp0, fp2, status); /* R */

        /* EXPR */
        fp1 = floatx80_mul(fp0, fp0, status); /* S = R*R */
        fp2 = float64_to_floatx80(make_float64(0x3F56C16D6F7BD0B2),
                                  status); /* A5 */
        fp3 = float64_to_floatx80(make_float64(0x3F811112302C712C),
                                  status); /* A4 */
        fp2 = floatx80_mul(fp2, fp1, status); /* S*A5 */
        fp3 = floatx80_mul(fp3, fp1, status); /* S*A4 */
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0x3FA5555555554CC1), status),
                           status); /* A3+S*A5 */
        fp3 = floatx80_add(fp3, float64_to_floatx80(
                           make_float64(0x3FC5555555554A54), status),
                           status); /* A2+S*A4 */
        fp2 = floatx80_mul(fp2, fp1, status); /* S*(A3+S*A5) */
        fp3 = floatx80_mul(fp3, fp1, status); /* S*(A2+S*A4) */
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0x3FE0000000000000), status),
                           status); /* A1+S*(A3+S*A5) */
        fp3 = floatx80_mul(fp3, fp0, status); /* R*S*(A2+S*A4) */

        fp2 = floatx80_mul(fp2, fp1, status); /* S*(A1+S*(A3+S*A5)) */
        fp0 = floatx80_add(fp0, fp3, status); /* R+R*S*(A2+S*A4) */
        fp0 = floatx80_add(fp0, fp2, status); /* EXP(R) - 1 */

        fp0 = floatx80_mul(fp0, fact1, status);
        fp0 = floatx80_add(fp0, fact2, status);
        fp0 = floatx80_add(fp0, fact1, status);

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_mul(fp0, adjfact, status);

        float_raise(float_flag_inexact, status);

        return a;
    }
}
