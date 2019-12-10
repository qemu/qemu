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
#include "imported/myfenv.h"
#include "imported/global_types.h"
#include "macros.h"
#include <stdio.h>
#include <assert.h>
#include "conv_emu.h"

#define isz(X) (fpclassify(X) == FP_ZERO)
#define DF_BIAS 1023
#define SF_BIAS 127

#define LL_MAX_POS 0x7fffffffffffffffULL
#define MAX_POS 0x7fffffffU

#ifdef VCPP
/*
 * Visual C isn't GNU C and doesn't have __builtin_clzll
 */

static int __builtin_clzll(unsigned long long int input)
{
    int total = 0;
    if (input == 0) {
        return 64;
    }
    total += ((input >> (total + 32)) != 0) ? 32 : 0;
    total += ((input >> (total + 16)) != 0) ? 16 : 0;
    total += ((input >> (total +  8)) != 0) ?  8 : 0;
    total += ((input >> (total +  4)) != 0) ?  4 : 0;
    total += ((input >> (total +  2)) != 0) ?  2 : 0;
    total += ((input >> (total +  1)) != 0) ?  1 : 0;
    return 63 - total;
}
#endif

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


#define MAKE_CONV_8U_TO_XF_N(FLOATID, BIGFLOATID, RETTYPE) \
static RETTYPE conv_8u_to_##FLOATID##_n(size8u_t in, int negate) \
{ \
    FLOATID##_t x; \
    size8u_t tmp, truncbits, shamt; \
    int leading_zeros; \
    if (in == 0) { \
        return 0.0; \
    } \
    leading_zeros = __builtin_clzll(in); \
    tmp = in << (leading_zeros); \
    tmp <<= 1; \
    shamt = 64 - f##BIGFLOATID##_MANTBITS(); \
    truncbits = tmp & ((1ULL << (shamt)) - 1); \
    tmp >>= shamt; \
    if (truncbits != 0) { \
        feraiseexcept(FE_INEXACT); \
        switch (fegetround()) { \
        case FE_TOWARDZERO: \
            break; \
        case FE_DOWNWARD: \
            if (negate) { \
                tmp += 1; \
            } \
            break; \
        case FE_UPWARD: \
            if (!negate) { \
                tmp += 1; \
            } \
            break; \
        default: \
            if ((truncbits & ((1ULL << (shamt - 1)) - 1)) == 0) { \
                tmp += (tmp & 1); \
            } else { \
                tmp += ((truncbits >> (shamt - 1)) & 1); \
            } \
            break; \
        } \
    } \
    if (((tmp << shamt) >> shamt) != tmp) { \
        leading_zeros--; \
    } \
    x.x.mant = tmp; \
    x.x.exp = BIGFLOATID##_BIAS + f##BIGFLOATID##_MANTBITS() - \
              leading_zeros + shamt - 1; \
    x.x.sign = negate; \
    return x.f; \
}

MAKE_CONV_8U_TO_XF_N(df, DF, double)
MAKE_CONV_8U_TO_XF_N(sf, SF, float)

double conv_8u_to_df(size8u_t in)
{
    return conv_8u_to_df_n(in, 0);
}

double conv_8s_to_df(size8s_t in)
{
    if (in == 0x8000000000000000) {
        return -0x1p63;
    }
    if (in < 0) {
        return conv_8u_to_df_n(-in, 1);
    } else {
        return conv_8u_to_df_n(in, 0);
    }
}

double conv_4u_to_df(size4u_t in)
{
    return conv_8u_to_df((size8u_t) in);
}

double conv_4s_to_df(size4s_t in)
{
    return conv_8s_to_df(in);
}

float conv_8u_to_sf(size8u_t in)
{
    return conv_8u_to_sf_n(in, 0);
}

float conv_8s_to_sf(size8s_t in)
{
    if (in == 0x8000000000000000) {
        return -0x1p63;
    }
    if (in < 0) {
        return conv_8u_to_sf_n(-in, 1);
    } else {
        return conv_8u_to_sf_n(in, 0);
    }
}

float conv_4u_to_sf(size4u_t in)
{
    return conv_8u_to_sf(in);
}

float conv_4s_to_sf(size4s_t in)
{
    return conv_8s_to_sf(in);
}


static size8u_t conv_df_to_8u_n(double in, int will_negate)
{
    df_t x;
    int fracshift, endshift;
    size8u_t tmp, truncbits;
    x.f = in;
    if (isinf(in)) {
        feraiseexcept(FE_INVALID);
        if (in > 0.0) {
            return ~0ULL;
        } else {
            return 0ULL;
        }
    }
    if (isnan(in)) {
        feraiseexcept(FE_INVALID);
        return ~0ULL;
    }
    if (isz(in)) {
        return 0;
    }
    if (x.x.sign) {
        feraiseexcept(FE_INVALID);
        return 0;
    }
    if (in < 0.5) {
        /* Near zero, captures large fracshifts, denorms, etc */
        feraiseexcept(FE_INEXACT);
        switch (fegetround()) {
        case FE_DOWNWARD:
            if (will_negate) {
                return 1;
            } else {
                return 0;
            }
        case FE_UPWARD:
            if (!will_negate) {
                return 1;
            } else {
                return 0;
            }
        default:
            return 0;    /* nearest or towards zero */
        }
    }
    if ((x.x.exp - DF_BIAS) >= 64) {
        /* way too big */
        feraiseexcept(FE_INVALID);
        return ~0ULL;
    }
    fracshift = fMAX(0, (fDF_MANTBITS() - (x.x.exp - DF_BIAS)));
    endshift = fMAX(0, ((x.x.exp - DF_BIAS - fDF_MANTBITS())));
    tmp = x.x.mant | (1ULL << fDF_MANTBITS());
    truncbits = tmp & ((1ULL << fracshift) - 1);
    tmp >>= fracshift;
    if (truncbits) {
        /* Apply Rounding */
        feraiseexcept(FE_INEXACT);
        switch (fegetround()) {
        case FE_TOWARDZERO:
            break;
        case FE_DOWNWARD:
            if (will_negate) {
                tmp += 1;
            }
            break;
        case FE_UPWARD:
            if (!will_negate) {
                tmp += 1;
            }
            break;
        default:
            if ((truncbits & ((1ULL << (fracshift - 1)) - 1)) == 0) {
                /* Exactly .5 */
                tmp += (tmp & 1);
            } else {
                tmp += ((truncbits >> (fracshift - 1)) & 1);
            }
        }
    }
    /*
     * If we added one and it carried all the way out,
     * check to see if overflow
     */
    if ((tmp & ((1ULL << (fDF_MANTBITS() + 1)) - 1)) == 0) {
        if ((x.x.exp - DF_BIAS) == 63) {
            feclearexcept(FE_INEXACT);
            feraiseexcept(FE_INVALID);
            return ~0ULL;
        }
    }
    tmp <<= endshift;
    return tmp;
}

static size4u_t conv_df_to_4u_n(double in, int will_negate)
{
    size8u_t tmp;
    tmp = conv_df_to_8u_n(in, will_negate);
    if (tmp > 0x00000000ffffffffULL) {
        feclearexcept(FE_INEXACT);
        feraiseexcept(FE_INVALID);
        return ~0U;
    }
    return (size4u_t)tmp;
}

size8u_t conv_df_to_8u(double in)
{
    return conv_df_to_8u_n(in, 0);
}

size4u_t conv_df_to_4u(double in)
{
    return conv_df_to_4u_n(in, 0);
}

size8s_t conv_df_to_8s(double in)
{
    size8u_t tmp;
    df_t x;
    x.f = in;
    if (isnan(in)) {
        feraiseexcept(FE_INVALID);
        return -1;
    }
    if (x.x.sign) {
        tmp = conv_df_to_8u_n(-in, 1);
    } else {
        tmp = conv_df_to_8u_n(in, 0);
    }
    if (tmp > (LL_MAX_POS + x.x.sign)) {
        feclearexcept(FE_INEXACT);
        feraiseexcept(FE_INVALID);
        tmp = (LL_MAX_POS + x.x.sign);
    }
    if (x.x.sign) {
        return -tmp;
    } else {
        return tmp;
    }
}

size4s_t conv_df_to_4s(double in)
{
    size8u_t tmp;
    df_t x;
    x.f = in;
    if (isnan(in)) {
        feraiseexcept(FE_INVALID);
        return -1;
    }
    if (x.x.sign) {
        tmp = conv_df_to_8u_n(-in, 1);
    } else {
        tmp = conv_df_to_8u_n(in, 0);
    }
    if (tmp > (MAX_POS + x.x.sign)) {
        feclearexcept(FE_INEXACT);
        feraiseexcept(FE_INVALID);
        tmp = (MAX_POS + x.x.sign);
    }
    if (x.x.sign) {
        return -tmp;
    } else {
        return tmp;
    }
}

size8u_t conv_sf_to_8u(float in)
{
    return conv_df_to_8u(in);
}

size4u_t conv_sf_to_4u(float in)
{
    return conv_df_to_4u(in);
}

size8s_t conv_sf_to_8s(float in)
{
    return conv_df_to_8s(in);
}

size4s_t conv_sf_to_4s(float in)
{
    return conv_df_to_4s(in);
}

