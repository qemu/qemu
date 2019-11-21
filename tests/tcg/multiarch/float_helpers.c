/*
 * Common Float Helpers
 *
 * This contains a series of useful utility routines and a set of
 * floating point constants useful for exercising the edge cases in
 * floating point tests.
 *
 * Copyright (c) 2019 Linaro
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* we want additional float type definitions */
#define __STDC_WANT_IEC_60559_BFP_EXT__
#define __STDC_WANT_IEC_60559_TYPES_EXT__

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <math.h>
#include <float.h>
#include <fenv.h>

#include "float_helpers.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/*
 * Half Precision Numbers
 *
 * Not yet well standardised so we return a plain uint16_t for now.
 */

/* no handy defines for these numbers */
static uint16_t f16_numbers[] = {
    0xffff, /* -NaN / AHP -Max */
    0xfcff, /* -NaN / AHP */
    0xfc01, /* -NaN / AHP */
    0xfc00, /* -Inf */
    0xfbff, /* -Max */
    0xc000, /* -2 */
    0xbc00, /* -1 */
    0x8001, /* -MIN subnormal */
    0x8000, /* -0 */
    0x0000, /* +0 */
    0x0001, /* MIN subnormal */
    0x3c00, /* 1 */
    0x7bff, /* Max */
    0x7c00, /* Inf */
    0x7c01, /* NaN / AHP */
    0x7cff, /* NaN / AHP */
    0x7fff, /* NaN / AHP +Max*/
};

static const int num_f16 = ARRAY_SIZE(f16_numbers);

int get_num_f16(void)
{
    return num_f16;
}

uint16_t get_f16(int i)
{
    return f16_numbers[i % num_f16];
}

/* only display as hex */
char *fmt_16(uint16_t num)
{
    char *fmt;
    asprintf(&fmt, "f16(%#04x)", num);
    return fmt;
}

/*
 * Single Precision Numbers
 */

#ifndef SNANF
/* Signaling NaN macros, if supported.  */
#  define SNANF (__builtin_nansf (""))
#  define SNAN (__builtin_nans (""))
#  define SNANL (__builtin_nansl (""))
#endif

static float f32_numbers[] = {
    -SNANF,
    -NAN,
    -INFINITY,
    -FLT_MAX,
    -0x1.1874b2p+103,
    -0x1.c0bab6p+99,
    -0x1.31f75p-40,
    -0x1.505444p-66,
    -FLT_MIN,
    0.0,
    FLT_MIN,
    0x1p-25,
    0x1.ffffe6p-25, /* min positive FP16 subnormal */
    0x1.ff801ap-15, /* max subnormal FP16 */
    0x1.00000cp-14, /* min positive normal FP16 */
    1.0,
    0x1.004p+0, /* smallest float after 1.0 FP16 */
    2.0,
    M_E, M_PI,
    0x1.ffbep+15,
    0x1.ffcp+15, /* max FP16 */
    0x1.ffc2p+15,
    0x1.ffbfp+16,
    0x1.ffcp+16, /* max AFP */
    0x1.ffc1p+16,
    0x1.c0bab6p+99,
    FLT_MAX,
    INFINITY,
    NAN,
    SNANF
};

static const int num_f32 = ARRAY_SIZE(f32_numbers);

int get_num_f32(void)
{
    return num_f32;
}

float get_f32(int i)
{
    return f32_numbers[i % num_f32];
}

char *fmt_f32(float num)
{
    uint32_t single_as_hex = *(uint32_t *) &num;
    char *fmt;
    asprintf(&fmt, "f32(%02.20a:%#010x)", num, single_as_hex);
    return fmt;
}


/* This allows us to initialise some doubles as pure hex */
typedef union {
    double d;
    uint64_t h;
} test_doubles;

static test_doubles f64_numbers[] = {
    {SNAN},
    {-NAN},
    {-INFINITY},
    {-DBL_MAX},
    {-FLT_MAX-1.0},
    {-FLT_MAX},
    {-1.111E+31},
    {-1.111E+30}, /* half prec */
    {-2.0}, {-1.0},
    {-DBL_MIN},
    {-FLT_MIN},
    {0.0},
    {FLT_MIN},
    {2.98023224e-08},
    {5.96046E-8}, /* min positive FP16 subnormal */
    {6.09756E-5}, /* max subnormal FP16 */
    {6.10352E-5}, /* min positive normal FP16 */
    {1.0},
    {1.0009765625}, /* smallest float after 1.0 FP16 */
    {DBL_MIN},
    {1.3789972848607228e-308},
    {1.4914738736681624e-308},
    {1.0}, {2.0},
    {M_E}, {M_PI},
    {65503.0},
    {65504.0}, /* max FP16 */
    {65505.0},
    {131007.0},
    {131008.0}, /* max AFP */
    {131009.0},
    {.h = 0x41dfffffffc00000 }, /* to int = 0x7fffffff */
    {FLT_MAX},
    {FLT_MAX + 1.0},
    {DBL_MAX},
    {INFINITY},
    {NAN},
    {.h = 0x7ff0000000000001}, /* SNAN */
    {SNAN},
};

static const int num_f64 = ARRAY_SIZE(f64_numbers);

int get_num_f64(void)
{
    return num_f64;
}

double get_f64(int i)
{
    return f64_numbers[i % num_f64].d;
}

char *fmt_f64(double num)
{
    uint64_t double_as_hex = *(uint64_t *) &num;
    char *fmt;
    asprintf(&fmt, "f64(%02.20a:%#020" PRIx64 ")", num, double_as_hex);
    return fmt;
}

/*
 * Float flags
 */
char *fmt_flags(void)
{
    int flags = fetestexcept(FE_ALL_EXCEPT);
    char *fmt;

    if (flags) {
        asprintf(&fmt, "%s%s%s%s%s",
                 flags & FE_OVERFLOW ? "OVERFLOW " : "",
                 flags & FE_UNDERFLOW ? "UNDERFLOW " : "",
                 flags & FE_DIVBYZERO ? "DIV0 " : "",
                 flags & FE_INEXACT ? "INEXACT " : "",
                 flags & FE_INVALID ? "INVALID" : "");
    } else {
        asprintf(&fmt, "OK");
    }

    return fmt;
}
