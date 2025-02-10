/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/int128.h"
#include "fpu/softfloat.h"
#include "macros.h"
#include "fma_emu.h"

#define DF_INF_EXP     0x7ff
#define DF_BIAS        1023
#define DF_MANTBITS    52
#define DF_NAN         0xffffffffffffffffULL
#define DF_INF         0x7ff0000000000000ULL
#define DF_MINUS_INF   0xfff0000000000000ULL
#define DF_MAXF        0x7fefffffffffffffULL
#define DF_MINUS_MAXF  0xffefffffffffffffULL

#define SF_INF_EXP     0xff
#define SF_BIAS        127
#define SF_MANTBITS    23
#define SF_INF         0x7f800000
#define SF_MINUS_INF   0xff800000
#define SF_MAXF        0x7f7fffff
#define SF_MINUS_MAXF  0xff7fffff

#define HF_INF_EXP 0x1f
#define HF_BIAS 15

#define WAY_BIG_EXP 4096

static uint64_t float64_getmant(float64 f64)
{
    uint64_t mant = extract64(f64, 0, 52);
    if (float64_is_normal(f64)) {
        return mant | 1ULL << 52;
    }
    if (float64_is_zero(f64)) {
        return 0;
    }
    if (float64_is_denormal(f64)) {
        return mant;
    }
    return ~0ULL;
}

int32_t float64_getexp(float64 f64)
{
    int exp = extract64(f64, 52, 11);
    if (float64_is_normal(f64)) {
        return exp;
    }
    if (float64_is_denormal(f64)) {
        return exp + 1;
    }
    return -1;
}

int32_t float32_getexp(float32 f32)
{
    int exp = float32_getexp_raw(f32);
    if (float32_is_normal(f32)) {
        return exp;
    }
    if (float32_is_denormal(f32)) {
        return exp + 1;
    }
    return -1;
}

static Int128 int128_mul_6464(uint64_t ai, uint64_t bi)
{
    uint64_t l, h;

    mulu64(&l, &h, ai, bi);
    return int128_make128(l, h);
}

static Int128 int128_sub_borrow(Int128 a, Int128 b, int borrow)
{
    Int128 ret = int128_sub(a, b);
    if (borrow != 0) {
        ret = int128_sub(ret, int128_one());
    }
    return ret;
}

typedef struct {
    Int128 mant;
    int32_t exp;
    uint8_t sign;
    uint8_t guard;
    uint8_t round;
    uint8_t sticky;
} Accum;

static void accum_init(Accum *p)
{
    p->mant = int128_zero();
    p->exp = 0;
    p->sign = 0;
    p->guard = 0;
    p->round = 0;
    p->sticky = 0;
}

static Accum accum_norm_left(Accum a)
{
    a.exp--;
    a.mant = int128_lshift(a.mant, 1);
    a.mant = int128_or(a.mant, int128_make64(a.guard));
    a.guard = a.round;
    a.round = a.sticky;
    return a;
}

/* This function is marked inline for performance reasons */
static inline Accum accum_norm_right(Accum a, int amt)
{
    if (amt > 130) {
        a.sticky |=
            a.round | a.guard | int128_nz(a.mant);
        a.guard = a.round = 0;
        a.mant = int128_zero();
        a.exp += amt;
        return a;

    }
    while (amt >= 64) {
        a.sticky |= a.round | a.guard | (int128_getlo(a.mant) != 0);
        a.guard = (int128_getlo(a.mant) >> 63) & 1;
        a.round = (int128_getlo(a.mant) >> 62) & 1;
        a.mant = int128_make64(int128_gethi(a.mant));
        a.exp += 64;
        amt -= 64;
    }
    while (amt > 0) {
        a.exp++;
        a.sticky |= a.round;
        a.round = a.guard;
        a.guard = int128_getlo(a.mant) & 1;
        a.mant = int128_rshift(a.mant, 1);
        amt--;
    }
    return a;
}

/*
 * On the add/sub, we need to be able to shift out lots of bits, but need a
 * sticky bit for what was shifted out, I think.
 */
static Accum accum_add(Accum a, Accum b);

static Accum accum_sub(Accum a, Accum b, int negate)
{
    Accum ret;
    accum_init(&ret);
    int borrow;

    if (a.sign != b.sign) {
        b.sign = !b.sign;
        return accum_add(a, b);
    }
    if (b.exp > a.exp) {
        /* small - big == - (big - small) */
        return accum_sub(b, a, !negate);
    }
    if ((b.exp == a.exp) && (int128_gt(b.mant, a.mant))) {
        /* small - big == - (big - small) */
        return accum_sub(b, a, !negate);
    }

    while (a.exp > b.exp) {
        /* Try to normalize exponents: shrink a exponent and grow mantissa */
        if (int128_gethi(a.mant) & (1ULL << 62)) {
            /* Can't grow a any more */
            break;
        } else {
            a = accum_norm_left(a);
        }
    }

    while (a.exp > b.exp) {
        /* Try to normalize exponents: grow b exponent and shrink mantissa */
        /* Keep around shifted out bits... we might need those later */
        b = accum_norm_right(b, a.exp - b.exp);
    }

    if ((int128_gt(b.mant, a.mant))) {
        return accum_sub(b, a, !negate);
    }

    /* OK, now things should be normalized! */
    ret.sign = a.sign;
    ret.exp = a.exp;
    assert(!int128_gt(b.mant, a.mant));
    borrow = (b.round << 2) | (b.guard << 1) | b.sticky;
    ret.mant = int128_sub_borrow(a.mant, b.mant, (borrow != 0));
    borrow = 0 - borrow;
    ret.guard = (borrow >> 2) & 1;
    ret.round = (borrow >> 1) & 1;
    ret.sticky = (borrow >> 0) & 1;
    if (negate) {
        ret.sign = !ret.sign;
    }
    return ret;
}

static Accum accum_add(Accum a, Accum b)
{
    Accum ret;
    accum_init(&ret);
    if (a.sign != b.sign) {
        b.sign = !b.sign;
        return accum_sub(a, b, 0);
    }
    if (b.exp > a.exp) {
        /* small + big ==  (big + small) */
        return accum_add(b, a);
    }
    if ((b.exp == a.exp) && int128_gt(b.mant, a.mant)) {
        /* small + big ==  (big + small) */
        return accum_add(b, a);
    }

    while (a.exp > b.exp) {
        /* Try to normalize exponents: shrink a exponent and grow mantissa */
        if (int128_gethi(a.mant) & (1ULL << 62)) {
            /* Can't grow a any more */
            break;
        } else {
            a = accum_norm_left(a);
        }
    }

    while (a.exp > b.exp) {
        /* Try to normalize exponents: grow b exponent and shrink mantissa */
        /* Keep around shifted out bits... we might need those later */
        b = accum_norm_right(b, a.exp - b.exp);
    }

    /* OK, now things should be normalized! */
    if (int128_gt(b.mant, a.mant)) {
        return accum_add(b, a);
    };
    ret.sign = a.sign;
    ret.exp = a.exp;
    assert(!int128_gt(b.mant, a.mant));
    ret.mant = int128_add(a.mant, b.mant);
    ret.guard = b.guard;
    ret.round = b.round;
    ret.sticky = b.sticky;
    return ret;
}

/* Return an infinity with requested sign */
static float64 infinite_float64(uint8_t sign)
{
    if (sign) {
        return make_float64(DF_MINUS_INF);
    } else {
        return make_float64(DF_INF);
    }
}

/* Return a maximum finite value with requested sign */
static float64 maxfinite_float64(uint8_t sign)
{
    if (sign) {
        return make_float64(DF_MINUS_MAXF);
    } else {
        return make_float64(DF_MAXF);
    }
}

/* Return a zero value with requested sign */
static float64 zero_float64(uint8_t sign)
{
    if (sign) {
        return make_float64(0x8000000000000000);
    } else {
        return float64_zero;
    }
}

/* Return an infinity with the requested sign */
float32 infinite_float32(uint8_t sign)
{
    if (sign) {
        return make_float32(SF_MINUS_INF);
    } else {
        return make_float32(SF_INF);
    }
}

/* Return a maximum finite value with the requested sign */
static float64 accum_round_float64(Accum a, float_status *fp_status)
{
    uint64_t ret;

    if ((int128_gethi(a.mant) == 0) && (int128_getlo(a.mant) == 0)
        && ((a.guard | a.round | a.sticky) == 0)) {
        /* result zero */
        switch (fp_status->float_rounding_mode) {
        case float_round_down:
            return zero_float64(1);
        default:
            return zero_float64(0);
        }
    }
    /*
     * Normalize right
     * We want DF_MANTBITS bits of mantissa plus the leading one.
     * That means that we want DF_MANTBITS+1 bits, or 0x000000000000FF_FFFF
     * So we need to normalize right while the high word is non-zero and
     * while the low word is nonzero when masked with 0xffe0_0000_0000_0000
     */
    while ((int128_gethi(a.mant) != 0) ||
           ((int128_getlo(a.mant) >> (DF_MANTBITS + 1)) != 0)) {
        a = accum_norm_right(a, 1);
    }
    /*
     * OK, now normalize left
     * We want to normalize left until we have a leading one in bit 24
     * Theoretically, we only need to shift a maximum of one to the left if we
     * shifted out lots of bits from B, or if we had no shift / 1 shift sticky
     * should be 0
     */
    while ((int128_getlo(a.mant) & (1ULL << DF_MANTBITS)) == 0) {
        a = accum_norm_left(a);
    }
    /*
     * OK, now we might need to denormalize because of potential underflow.
     * We need to do this before rounding, and rounding might make us normal
     * again
     */
    while (a.exp <= 0) {
        a = accum_norm_right(a, 1 - a.exp);
        /*
         * Do we have underflow?
         * That's when we get an inexact answer because we ran out of bits
         * in a denormal.
         */
        if (a.guard || a.round || a.sticky) {
            float_raise(float_flag_underflow, fp_status);
        }
    }
    /* OK, we're relatively canonical... now we need to round */
    if (a.guard || a.round || a.sticky) {
        float_raise(float_flag_inexact, fp_status);
        switch (fp_status->float_rounding_mode) {
        case float_round_to_zero:
            /* Chop and we're done */
            break;
        case float_round_up:
            if (a.sign == 0) {
                a.mant = int128_add(a.mant, int128_one());
            }
            break;
        case float_round_down:
            if (a.sign != 0) {
                a.mant = int128_add(a.mant, int128_one());
            }
            break;
        default:
            if (a.round || a.sticky) {
                /* round up if guard is 1, down if guard is zero */
                a.mant = int128_add(a.mant, int128_make64(a.guard));
            } else if (a.guard) {
                /* exactly .5, round up if odd */
                a.mant = int128_add(a.mant, int128_and(a.mant, int128_one()));
            }
            break;
        }
    }
    /*
     * OK, now we might have carried all the way up.
     * So we might need to shr once
     * at least we know that the lsb should be zero if we rounded and
     * got a carry out...
     */
    if ((int128_getlo(a.mant) >> (DF_MANTBITS + 1)) != 0) {
        a = accum_norm_right(a, 1);
    }
    /* Overflow? */
    if (a.exp >= DF_INF_EXP) {
        /* Yep, inf result */
        float_raise(float_flag_overflow, fp_status);
        float_raise(float_flag_inexact, fp_status);
        switch (fp_status->float_rounding_mode) {
        case float_round_to_zero:
            return maxfinite_float64(a.sign);
        case float_round_up:
            if (a.sign == 0) {
                return infinite_float64(a.sign);
            } else {
                return maxfinite_float64(a.sign);
            }
        case float_round_down:
            if (a.sign != 0) {
                return infinite_float64(a.sign);
            } else {
                return maxfinite_float64(a.sign);
            }
        default:
            return infinite_float64(a.sign);
        }
    }
    /* Underflow? */
    ret = int128_getlo(a.mant);
    if (ret & (1ULL << DF_MANTBITS)) {
        /* Leading one means: No, we're normal. So, we should be done... */
        ret = deposit64(ret, 52, 11, a.exp);
    } else {
        assert(a.exp == 1);
        ret = deposit64(ret, 52, 11, 0);
    }
    ret = deposit64(ret, 63, 1, a.sign);
    return ret;
}

float64 internal_mpyhh(float64 a, float64 b,
                      unsigned long long int accumulated,
                      float_status *fp_status)
{
    Accum x;
    unsigned long long int prod;
    unsigned int sticky;
    uint8_t a_sign, b_sign;

    sticky = accumulated & 1;
    accumulated >>= 1;
    accum_init(&x);
    if (float64_is_zero(a) ||
        float64_is_any_nan(a) ||
        float64_is_infinity(a)) {
        return float64_mul(a, b, fp_status);
    }
    if (float64_is_zero(b) ||
        float64_is_any_nan(b) ||
        float64_is_infinity(b)) {
        return float64_mul(a, b, fp_status);
    }
    x.mant = int128_make64(accumulated);
    x.sticky = sticky;
    prod = fGETUWORD(1, float64_getmant(a)) * fGETUWORD(1, float64_getmant(b));
    x.mant = int128_add(x.mant, int128_mul_6464(prod, 0x100000000ULL));
    x.exp = float64_getexp(a) + float64_getexp(b) - DF_BIAS - 20;
    if (!float64_is_normal(a) || !float64_is_normal(b)) {
        /* crush to inexact zero */
        x.sticky = 1;
        x.exp = -4096;
    }
    a_sign = float64_is_neg(a);
    b_sign = float64_is_neg(b);
    x.sign = a_sign ^ b_sign;
    return accum_round_float64(x, fp_status);
}
