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
