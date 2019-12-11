/*
 *  Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#include <stdio.h>
#include <math.h>
#include "macros.h"
#include "conv_emu.h"
#include "fma_emu.h"

#define DF_INF_EXP 0x7ff
#define DF_BIAS 1023

#define SF_INF_EXP 0xff
#define SF_BIAS 127

#define HF_INF_EXP 0x1f
#define HF_BIAS 15

#define WAY_BIG_EXP 4096

#define isz(X) (fpclassify(X) == FP_ZERO)

#define debug_fprintf(...)    /* nothing */

typedef union {
    double f;
    size8u_t i;
    struct {
        size8u_t mant:52;
        size8u_t exp:11;
        size8u_t sign:1;
    } x;
} df_t;

typedef union {
    float f;
    size4u_t i;
    struct {
        size4u_t mant:23;
        size4u_t exp:8;
        size4u_t sign:1;
    } x;
} sf_t;

typedef struct {
    union {
        size8u_t low;
        struct {
            size4u_t w0;
            size4u_t w1;
        };
    };
    union {
        size8u_t high;
        struct {
            size4u_t w3;
            size4u_t w2;
        };
    };
} int128_t;

typedef struct {
    int128_t mant;
    size4s_t exp;
    size1u_t sign;
    size1u_t guard;
    size1u_t round;
    size1u_t sticky;
} xf_t;

static inline void xf_debug(const char *msg, xf_t a)
{
    debug_fprintf(stdout, "%s %c0x%016llx_%016llx /%d/%d/%d p%d\n", msg,
                  a.sign ? '-' : '+', a.mant.high, a.mant.low, a.guard,
                  a.round, a.sticky, a.exp);
}

static inline void xf_init(xf_t *p)
{
    p->mant.low = 0;
    p->mant.high = 0;
    p->exp = 0;
    p->sign = 0;
    p->guard = 0;
    p->round = 0;
    p->sticky = 0;
}

static inline size8u_t df_getmant(df_t a)
{
    int class = fpclassify(a.f);
    switch (class) {
    case FP_NORMAL:
    return a.x.mant | 1ULL << 52;
    case FP_ZERO:
        return 0;
    case FP_SUBNORMAL:
        return a.x.mant;
    default:
        return -1;
    };
}

static inline size4s_t df_getexp(df_t a)
{
    int class = fpclassify(a.f);
    switch (class) {
    case FP_NORMAL:
        return a.x.exp;
    case FP_SUBNORMAL:
        return a.x.exp + 1;
    default:
        return -1;
    };
}

static inline size8u_t sf_getmant(sf_t a)
{
    int class = fpclassify(a.f);
    switch (class) {
    case FP_NORMAL:
        return a.x.mant | 1ULL << 23;
    case FP_ZERO:
        return 0;
    case FP_SUBNORMAL:
        return a.x.mant | 0ULL;
    default:
        return -1;
    };
}

static inline size4s_t sf_getexp(sf_t a)
{
    int class = fpclassify(a.f);
    switch (class) {
    case FP_NORMAL:
        return a.x.exp;
    case FP_SUBNORMAL:
        return a.x.exp + 1;
    default:
        return -1;
    };
}

static inline int128_t int128_mul_6464(size8u_t ai, size8u_t bi)
{
    int128_t ret;
    int128_t a, b;
    size8u_t pp0, pp1a, pp1b, pp1s, pp2;

    debug_fprintf(stdout, "ai/bi: 0x%016llx/0x%016llx\n", ai, bi);

    a.high = b.high = 0;
    a.low = ai;
    b.low = bi;
    pp0 = (size8u_t)a.w0 * (size8u_t)b.w0;
    pp1a = (size8u_t)a.w1 * (size8u_t)b.w0;
    pp1b = (size8u_t)b.w1 * (size8u_t)a.w0;
    pp2 = (size8u_t)a.w1 * (size8u_t)b.w1;

    debug_fprintf(stdout,
                  "pp2/1b/1a/0: 0x%016llx/0x%016llx/0x%016llx/0x%016llx\n",
                  pp2, pp1b, pp1a, pp0);

    pp1s = pp1a + pp1b;
    if ((pp1s < pp1a) || (pp1s < pp1b)) {
        pp2 += (1ULL << 32);
    }
    ret.low = pp0 + (pp1s << 32);
    if ((ret.low < pp0) || (ret.low < (pp1s << 32))) {
        pp2 += 1;
    }
    ret.high = pp2 + (pp1s >> 32);

    debug_fprintf(stdout,
                  "pp1s/rethi/retlo: 0x%016llx/0x%016llx/0x%016llx\n",
                  pp1s, ret.high, ret.low);

    return ret;
}

static inline int128_t int128_shl(int128_t a, size4u_t amt)
{
    int128_t ret;
    if (amt == 0) {
        return a;
    }
    if (amt > 128) {
        ret.high = 0;
        ret.low = 0;
        return ret;
    }
    if (amt >= 64) {
        amt -= 64;
        a.high = a.low;
        a.low = 0;
    }
    ret.high = a.high << amt;
    ret.high |= (a.low >> (64 - amt));
    ret.low = a.low << amt;
    return ret;
}

static inline int128_t int128_shr(int128_t a, size4u_t amt)
{
    int128_t ret;
    if (amt == 0) {
        return a;
    }
    if (amt > 128) {
        ret.high = 0;
        ret.low = 0;
        return ret;
    }
    if (amt >= 64) {
        amt -= 64;
        a.low = a.high;
        a.high = 0;
    }
    ret.low = a.low >> amt;
    ret.low |= (a.high << (64 - amt));
    ret.high = a.high >> amt;
    return ret;
}

static inline int128_t int128_add(int128_t a, int128_t b)
{
    int128_t ret;
    ret.low = a.low + b.low;
    if ((ret.low < a.low) || (ret.low < b.low)) {
        /* carry into high part */
        a.high += 1;
    }
    ret.high = a.high + b.high;
    return ret;
}

static inline int128_t int128_sub(int128_t a, int128_t b, int borrow)
{
    int128_t ret;
    ret.low = a.low - b.low;
    if (ret.low > a.low) {
        /* borrow into high part */
        a.high -= 1;
    }
    ret.high = a.high - b.high;
    if (borrow == 0) {
        return ret;
    } else {
        a.high = 0;
        a.low = 1;
        return int128_sub(ret, a, 0);
    }
}

static inline int int128_gt(int128_t a, int128_t b)
{
    if (a.high == b.high) {
        return a.low > b.low;
    }
    return a.high > b.high;
}

static inline xf_t xf_norm_left(xf_t a)
{
    a.exp--;
    a.mant = int128_shl(a.mant, 1);
    a.mant.low |= a.guard;
    a.guard = a.round;
    a.round = a.sticky;
    return a;
}

static inline xf_t xf_norm_right(xf_t a, int amt)
{
    if (amt > 130) {
        a.sticky |=
            a.round | a.guard | (a.mant.low != 0) | (a.mant.high != 0);
        a.guard = a.round = a.mant.high = a.mant.low = 0;
        a.exp += amt;
        return a;

    }
    while (amt >= 64) {
        a.sticky |= a.round | a.guard | (a.mant.low != 0);
        a.guard = (a.mant.low >> 63) & 1;
        a.round = (a.mant.low >> 62) & 1;
        a.mant.low = a.mant.high;
        a.mant.high = 0;
        a.exp += 64;
        amt -= 64;
    }
    while (amt > 0) {
        a.exp++;
        a.sticky |= a.round;
        a.round = a.guard;
        a.guard = a.mant.low & 1;
        a.mant = int128_shr(a.mant, 1);
        amt--;
    }
    return a;
}


/*
 * On the add/sub, we need to be able to shift out lots of bits, but need a
 * sticky bit for what was shifted out, I think.
 */
static xf_t xf_add(xf_t a, xf_t b);

static inline xf_t xf_sub(xf_t a, xf_t b, int negate)
{
    xf_t ret;
    xf_init(&ret);
    int borrow;

    xf_debug("-->Sub/a: ", a);
    xf_debug("-->Sub/b: ", b);

    if (a.sign != b.sign) {
        b.sign = !b.sign;
        return xf_add(a, b);
    }
    if (b.exp > a.exp) {
        /* small - big == - (big - small) */
        return xf_sub(b, a, !negate);
    }
    if ((b.exp == a.exp) && (int128_gt(b.mant, a.mant))) {
        /* small - big == - (big - small) */
        return xf_sub(b, a, !negate);
    }

    xf_debug("OK: Sub/a: ", a);
    xf_debug("OK: Sub/b: ", b);

    while (a.exp > b.exp) {
        /* Try to normalize exponents: shrink a exponent and grow mantissa */
        if (a.mant.high & (1ULL << 62)) {
            /* Can't grow a any more */
            break;
        } else {
            a = xf_norm_left(a);
        }
    }

    xf_debug("norm_l: Sub/a: ", a);
    xf_debug("norm_l: Sub/b: ", b);

    while (a.exp > b.exp) {
        /* Try to normalize exponents: grow b exponent and shrink mantissa */
        /* Keep around shifted out bits... we might need those later */
        b = xf_norm_right(b, a.exp - b.exp);
    }

    xf_debug("norm_r: Sub/a: ", a);
    xf_debug("norm_r: Sub/b: ", b);

    if ((int128_gt(b.mant, a.mant))) {
        xf_debug("retry: Sub/a: ", a);
        xf_debug("retry: Sub/b: ", b);
        return xf_sub(b, a, !negate);
    }

    /* OK, now things should be normalized! */
    ret.sign = a.sign;
    ret.exp = a.exp;
    assert(!int128_gt(b.mant, a.mant));
    borrow = (b.round << 2) | (b.guard << 1) | b.sticky;
    ret.mant = int128_sub(a.mant, b.mant, (borrow != 0));
    borrow = 0 - borrow;
    ret.guard = (borrow >> 2) & 1;
    ret.round = (borrow >> 1) & 1;
    ret.sticky = (borrow >> 0) & 1;
    if (negate) {
        ret.sign = !ret.sign;
    }
    return ret;
}

static xf_t xf_add(xf_t a, xf_t b)
{
    xf_t ret;
    xf_init(&ret);
    xf_debug("-->Add/a: ", a);
    xf_debug("-->Add/b: ", b);
    if (a.sign != b.sign) {
        b.sign = !b.sign;
        return xf_sub(a, b, 0);
    }
    if (b.exp > a.exp) {
        /* small + big ==  (big + small) */
        return xf_add(b, a);
    }
    if ((b.exp == a.exp) && int128_gt(b.mant, a.mant)) {
        /* small + big ==  (big + small) */
        return xf_add(b, a);
    }

    xf_debug("OK? Add/a: ", a);
    xf_debug("OK? Add/b: ", b);

    while (a.exp > b.exp) {
        /* Try to normalize exponents: shrink a exponent and grow mantissa */
        if (a.mant.high & (1ULL << 62)) {
            /* Can't grow a any more */
            break;
        } else {
            a = xf_norm_left(a);
        }
    }

    xf_debug("norm_l: Add/a: ", a);
    xf_debug("norm_l: Add/b: ", b);

    while (a.exp > b.exp) {
        /* Try to normalize exponents: grow b exponent and shrink mantissa */
        /* Keep around shifted out bits... we might need those later */
        b = xf_norm_right(b, a.exp - b.exp);
    }

    xf_debug("norm_r: Add/a: ", a);
    xf_debug("norm_r: Add/b: ", b);

    /* OK, now things should be normalized! */
    if (int128_gt(b.mant, a.mant)) {
        xf_debug("retry: Add/a: ", a);
        xf_debug("retry: Add/b: ", b);
        return xf_add(b, a);
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

/* Return an infinity with the same sign as a */
static inline df_t infinite_df_t(xf_t a)
{
    df_t ret;
    ret.x.sign = a.sign;
    ret.x.exp = DF_INF_EXP;
    ret.x.mant = 0ULL;
    return ret;
}

/* Return a maximum finite value with the same sign as a */
static inline df_t maxfinite_df_t(xf_t a)
{
    df_t ret;
    ret.x.sign = a.sign;
    ret.x.exp = DF_INF_EXP - 1;
    ret.x.mant = 0x000fffffffffffffULL;
    return ret;
}

static inline df_t f2df_t(double in)
{
    df_t ret;
    ret.f = in;
    return ret;
}

/* Return an infinity with the same sign as a */
static inline sf_t infinite_sf_t(xf_t a)
{
    sf_t ret;
    ret.x.sign = a.sign;
    ret.x.exp = SF_INF_EXP;
    ret.x.mant = 0ULL;
    return ret;
}

/* Return a maximum finite value with the same sign as a */
static inline sf_t maxfinite_sf_t(xf_t a)
{
    sf_t ret;
    ret.x.sign = a.sign;
    ret.x.exp = SF_INF_EXP - 1;
    ret.x.mant = 0x007fffffUL;
    return ret;
}

static inline sf_t f2sf_t(float in)
{
    sf_t ret;
    ret.f = in;
    return ret;
}

#define GEN_XF_ROUND(TYPE, MANTBITS, INF_EXP) \
static inline TYPE xf_round_##TYPE(xf_t a) \
{ \
    TYPE ret; \
    ret.i = 0; \
    ret.x.sign = a.sign; \
    if ((a.mant.high == 0) && (a.mant.low == 0) \
        && ((a.guard | a.round | a.sticky) == 0)) { \
        /* result zero */ \
        switch (fegetround()) { \
        case FE_DOWNWARD: \
            return f2##TYPE(-0.0); \
        default: \
            return f2##TYPE(0.0); \
        } \
    } \
    /* Normalize right */ \
    /* We want MANTBITS bits of mantissa plus the leading one. */ \
    /* That means that we want MANTBITS+1 bits, or 0x000000000000FF_FFFF */ \
    /* So we need to normalize right while the high word is non-zero and \
    * while the low word is nonzero when masked with 0xffe0_0000_0000_0000 */ \
    xf_debug("input: ", a); \
    while ((a.mant.high != 0) || ((a.mant.low >> (MANTBITS + 1)) != 0)) { \
        a = xf_norm_right(a, 1); \
    } \
    xf_debug("norm_right: ", a); \
    /* \
     * OK, now normalize left \
     * We want to normalize left until we have a leading one in bit 24 \
     * Theoretically, we only need to shift a maximum of one to the left if we \
     * shifted out lots of bits from B, or if we had no shift / 1 shift sticky \
     * shoudl be 0  \
     */ \
    while ((a.mant.low & (1ULL << MANTBITS)) == 0) { \
        a = xf_norm_left(a); \
    } \
    xf_debug("norm_left: ", a); \
    /* \
     * OK, now we might need to denormalize because of potential underflow. \
     * We need to do this before rounding, and rounding might make us normal \
     * again \
     */ \
    while (a.exp <= 0) { \
        a = xf_norm_right(a, 1 - a.exp); \
        /* \
         * Do we have underflow? \
         * That's when we get an inexact answer because we ran out of bits \
         * in a denormal. \
         */ \
        if (a.guard || a.round || a.sticky) { \
            feraiseexcept(FE_UNDERFLOW); \
        } \
    } \
    xf_debug("norm_denorm: ", a); \
    /* OK, we're relatively canonical... now we need to round */ \
    if (a.guard || a.round || a.sticky) { \
        feraiseexcept(FE_INEXACT); \
        switch (fegetround()) { \
        case FE_TOWARDZERO: \
            /* Chop and we're done */ \
            break; \
        case FE_UPWARD: \
            if (a.sign == 0) { \
                a.mant.low += 1; \
            } \
            break; \
        case FE_DOWNWARD: \
            if (a.sign != 0) { \
                a.mant.low += 1; \
            } \
            break; \
        default: \
            if (a.round || a.sticky) { \
                /* round up if guard is 1, down if guard is zero */ \
                a.mant.low += a.guard; \
            } else if (a.guard) { \
                /* exactly .5, round up if odd */ \
                a.mant.low += (a.mant.low & 1); \
            } \
            break; \
        } \
    } \
    xf_debug("post_round: ", a); \
    /* \
     * OK, now we might have carried all the way up. \
     * So we might need to shr once \
     * at least we know that the lsb should be zero if we rounded and \
     * got a carry out... \
     */ \
    if ((a.mant.low >> (MANTBITS + 1)) != 0) { \
        a = xf_norm_right(a, 1); \
    } \
    xf_debug("once_norm_right: ", a); \
    /* Overflow? */ \
    if (a.exp >= INF_EXP) { \
        /* Yep, inf result */ \
        xf_debug("inf: ", a); \
        feraiseexcept(FE_OVERFLOW); \
        feraiseexcept(FE_INEXACT); \
        switch (fegetround()) { \
        case FE_TOWARDZERO: \
            return maxfinite_##TYPE(a); \
        case FE_UPWARD: \
            if (a.sign == 0) { \
                return infinite_##TYPE(a); \
            } else { \
                return maxfinite_##TYPE(a); \
            } \
        case FE_DOWNWARD: \
            if (a.sign != 0) { \
                return infinite_##TYPE(a); \
            } else { \
                return maxfinite_##TYPE(a); \
            } \
        default: \
            return infinite_##TYPE(a); \
        } \
    } \
    /* Underflow? */ \
    if (a.mant.low & (1ULL << MANTBITS)) { \
        /* Leading one means: No, we're normal. So, we should be done... */ \
        xf_debug("norm: ", a); \
        ret.x.exp = a.exp; \
        ret.x.mant = a.mant.low; \
        return ret; \
    } \
    xf_debug("denorm: ", a); \
    if (a.exp != 1) { \
        printf("a.exp == %d\n", a.exp); \
    } \
    assert(a.exp == 1); \
    ret.x.exp = 0; \
    ret.x.mant = a.mant.low; \
    return ret; \
}

GEN_XF_ROUND(df_t, fDF_MANTBITS(), DF_INF_EXP)
GEN_XF_ROUND(sf_t, fSF_MANTBITS(), SF_INF_EXP)

static inline double special_fma(df_t a, df_t b, df_t c)
{
    df_t ret;
    ret.i = 0;

    /*
     * If A multiplied by B is an exact infinity and C is also an infinity
     * but with the opposite sign, FMA returns NaN and raises invalid.
     */
    if (fISINFPROD(a.f, b.f) && isinf(c.f)) {
        if ((a.x.sign ^ b.x.sign) != c.x.sign) {
            ret.i = fDFNANVAL();
            feraiseexcept(FE_INVALID);
            return ret.f;
        }
    }
    if ((isinf(a.f) && isz(b.f)) || (isz(a.f) && isinf(b.f))) {
        ret.i = fDFNANVAL();
        feraiseexcept(FE_INVALID);
        return ret.f;
    }
    /*
     * If none of the above checks are true and C is a NaN,
     * a NaN shall be returned
     * If A or B are NaN, a NAN shall be returned.
     */
    if (isnan(a.f) || isnan(b.f) || (isnan(c.f))) {
        if (isnan(a.f) && (fGETBIT(51, a.i) == 0)) {
            feraiseexcept(FE_INVALID);
        }
        if (isnan(b.f) && (fGETBIT(51, b.i) == 0)) {
            feraiseexcept(FE_INVALID);
        }
        if (isnan(c.f) && (fGETBIT(51, c.i) == 0)) {
            feraiseexcept(FE_INVALID);
        }
        ret.i = fDFNANVAL();
        return ret.f;
    }
    /*
     * We have checked for adding opposite-signed infinities.
     * Other infinities return infinity with the correct sign
     */
    if (isinf(c.f)) {
        ret.x.exp = DF_INF_EXP;
        ret.x.mant = 0;
        ret.x.sign = c.x.sign;
        return ret.f;
    }
    if (isinf(a.f) || isinf(b.f)) {
        ret.x.exp = DF_INF_EXP;
        ret.x.mant = 0;
        ret.x.sign = (a.x.sign ^ b.x.sign);
        return ret.f;
    }
    g_assert_not_reached();
    ret.x.exp = 0x123;
    ret.x.mant = 0xdead;
    return ret.f;
}

static inline float special_fmaf(sf_t a, sf_t b, sf_t c)
{
    df_t aa, bb, cc;
    aa.f = a.f;
    bb.f = b.f;
    cc.f = c.f;
    return special_fma(aa, bb, cc);
}

double internal_fmax(double a_in, double b_in, double c_in, int scale)
{
    df_t a, b, c;
    xf_t prod;
    xf_t acc;
    xf_t result;
    xf_init(&prod);
    xf_init(&acc);
    xf_init(&result);
    a.f = a_in;
    b.f = b_in;
    c.f = c_in;

    debug_fprintf(stdout,
                  "internal_fmax: 0x%016llx * 0x%016llx + 0x%016llx sc: %d\n",
                  fUNDOUBLE(a_in), fUNDOUBLE(b_in), fUNDOUBLE(c_in),
                  scale);

    if (isinf(a.f) || isinf(b.f) || isinf(c.f)) {
        return special_fma(a, b, c);
    }
    if (isnan(a.f) || isnan(b.f) || isnan(c.f)) {
        return special_fma(a, b, c);
    }
    if ((scale == 0) && (isz(a.f) || isz(b.f))) {
        return a.f * b.f + c.f;
    }

    /* (a * 2**b) * (c * 2**d) == a*c * 2**(b+d) */
    prod.mant = int128_mul_6464(df_getmant(a), df_getmant(b));

    /*
     * Note: extracting the mantissa into an int is multiplying by
     * 2**52, so adjust here
     */
    prod.exp = df_getexp(a) + df_getexp(b) - DF_BIAS - 52;
    prod.sign = a.x.sign ^ b.x.sign;
    xf_debug("prod: ", prod);
    if (!isz(c.f)) {
        acc.mant = int128_mul_6464(df_getmant(c), 1);
        acc.exp = df_getexp(c);
        acc.sign = c.x.sign;
        xf_debug("acc: ", acc);
        result = xf_add(prod, acc);
    } else {
        result = prod;
    }
    xf_debug("sum: ", result);
    debug_fprintf(stdout, "Scaling: %d\n", scale);
    result.exp += scale;
    xf_debug("post-scale: ", result);
    return xf_round_df_t(result).f;
}

float internal_fmafx(float a_in, float b_in, float c_in, int scale)
{
    sf_t a, b, c;
    xf_t prod;
    xf_t acc;
    xf_t result;
    xf_init(&prod);
    xf_init(&acc);
    xf_init(&result);
    a.f = a_in;
    b.f = b_in;
    c.f = c_in;

    debug_fprintf(stdout,
                  "internal_fmax: 0x%016x * 0x%016x + 0x%016x sc: %d\n",
                  fUNFLOAT(a_in), fUNFLOAT(b_in), fUNFLOAT(c_in), scale);
    if (isinf(a.f) || isinf(b.f) || isinf(c.f)) {
        return special_fmaf(a, b, c);
    }
    if (isnan(a.f) || isnan(b.f) || isnan(c.f)) {
        return special_fmaf(a, b, c);
    }
    if ((scale == 0) && (isz(a.f) || isz(b.f))) {
        return a.f * b.f + c.f;
    }

    /* (a * 2**b) * (c * 2**d) == a*c * 2**(b+d) */
    prod.mant = int128_mul_6464(sf_getmant(a), sf_getmant(b));

    /*
     * Note: extracting the mantissa into an int is multiplying by
     * 2**23, so adjust here
     */
    prod.exp = sf_getexp(a) + sf_getexp(b) - SF_BIAS - 23;
    prod.sign = a.x.sign ^ b.x.sign;
    if (isz(a.f) || isz(b.f)) {
        prod.exp = -2 * WAY_BIG_EXP;
    }
    xf_debug("prod: ", prod);
    if ((scale > 0) && (fpclassify(c.f) == FP_SUBNORMAL)) {
        acc.mant = int128_mul_6464(0, 0);
        acc.exp = -WAY_BIG_EXP;
        acc.sign = c.x.sign;
        acc.sticky = 1;
        xf_debug("special denorm acc: ", acc);
        result = xf_add(prod, acc);
    } else if (!isz(c.f)) {
        acc.mant = int128_mul_6464(sf_getmant(c), 1);
        acc.exp = sf_getexp(c);
        acc.sign = c.x.sign;
        xf_debug("acc: ", acc);
        result = xf_add(prod, acc);
    } else {
        result = prod;
    }
    xf_debug("sum: ", result);
    debug_fprintf(stdout, "Scaling: %d\n", scale);
    result.exp += scale;
    xf_debug("post-scale: ", result);
    return xf_round_sf_t(result).f;
}


float internal_fmaf(float a_in, float b_in, float c_in)
{
    return internal_fmafx(a_in, b_in, c_in, 0);
}

double internal_fma(double a_in, double b_in, double c_in)
{
    return internal_fmax(a_in, b_in, c_in, 0);
}

float internal_mpyf(float a_in, float b_in)
{
    if (isz(a_in) || isz(b_in)) {
        return a_in * b_in;
    }
    return internal_fmafx(a_in, b_in, 0.0, 0);
}

double internal_mpy(double a_in, double b_in)
{
    if (isz(a_in) || isz(b_in)) {
        return a_in * b_in;
    }
    return internal_fmax(a_in, b_in, 0.0, 0);
}

static inline double internal_mpyhh_special(double a, double b)
{
    return a * b;
}

double internal_mpyhh(double a_in, double b_in,
                      unsigned long long int accumulated)
{
    df_t a, b;
    xf_t x;
    unsigned long long int prod;
    unsigned int sticky;

    a.f = a_in;
    b.f = b_in;
    sticky = accumulated & 1;
    accumulated >>= 1;
    xf_init(&x);
    if (isz(a_in) || isnan(a_in) || isinf(a_in)) {
        return internal_mpyhh_special(a_in, b_in);
    }
    if (isz(b_in) || isnan(b_in) || isinf(b_in)) {
        return internal_mpyhh_special(a_in, b_in);
    }
    x.mant = int128_mul_6464(accumulated, 1);
    x.sticky = sticky;
    prod = fGETUWORD(1, df_getmant(a)) * fGETUWORD(1, df_getmant(b));
    x.mant = int128_add(x.mant, int128_mul_6464(prod, 0x100000000ULL));
    x.exp = df_getexp(a) + df_getexp(b) - DF_BIAS - 20;
    xf_debug("formed: ", x);
    if (!isnormal(a_in) || !isnormal(b_in)) {
        /* crush to inexact zero */
        x.sticky = 1;
        x.exp = -4096;
        xf_debug("crushing: ", x);
    }
    x.sign = a.x.sign ^ b.x.sign;
    xf_debug("with sign: ", x);
    return xf_round_df_t(x).f;
}

float conv_df_to_sf(double in_f)
{
    xf_t x;
    df_t in;
    if (isz(in_f) || isnan(in_f) || isinf(in_f)) {
        return in_f;
    }
    xf_init(&x);
    in.f = in_f;
    x.mant = int128_mul_6464(df_getmant(in), 1);
    x.exp = df_getexp(in) - DF_BIAS + SF_BIAS - 52 + 23;
    x.sign = in.x.sign;
    xf_debug("conv to sf: x: ", x);
    return xf_round_sf_t(x).f;
}

