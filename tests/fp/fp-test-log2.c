/*
 * fp-test-log2.c - test QEMU's softfloat log2
 *
 * Copyright (C) 2020, Linaro, Ltd.
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#ifndef HW_POISON_H
#error Must define HW_POISON_H to work around TARGET_* poisoning
#endif

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include <math.h>
#include "fpu/softfloat.h"

typedef union {
    double d;
    float64 i;
} ufloat64;

static int errors;

static void compare(ufloat64 test, ufloat64 real, ufloat64 soft, bool exact)
{
    int msb;
    uint64_t ulp = UINT64_MAX;

    if (real.i == soft.i) {
        return;
    }
    msb = 63 - __builtin_clzll(real.i ^ soft.i);

    if (msb < 52) {
        if (real.i > soft.i) {
            ulp = real.i - soft.i;
        } else {
            ulp = soft.i - real.i;
        }
    }

    /* glibc allows 3 ulp error in its libm-test-ulps; allow 4 here */
    if (!exact && ulp <= 4) {
        return;
    }

    printf("test: %016" PRIx64 "  %+.13a\n"
           "  sf: %016" PRIx64 "  %+.13a\n"
           "libm: %016" PRIx64 "  %+.13a\n",
           test.i, test.d, soft.i, soft.d, real.i, real.d);

    if (msb == 63) {
        printf("Error in sign!\n\n");
    } else if (msb >= 52) {
        printf("Error in exponent: %d\n\n",
               (int)(soft.i >> 52) - (int)(real.i >> 52));
    } else {
        printf("Error in fraction: %" PRIu64 " ulp\n\n", ulp);
    }

    if (++errors == 20) {
        exit(1);
    }
}

int main(int ac, char **av)
{
    ufloat64 test, real, soft;
    float_status qsf = {0};
    int i;

    set_float_rounding_mode(float_round_nearest_even, &qsf);

    test.d = 0.0;
    real.d = -__builtin_inf();
    soft.i = float64_log2(test.i, &qsf);
    compare(test, real, soft, true);

    test.d = 1.0;
    real.d = 0.0;
    soft.i = float64_log2(test.i, &qsf);
    compare(test, real, soft, true);

    test.d = 2.0;
    real.d = 1.0;
    soft.i = float64_log2(test.i, &qsf);
    compare(test, real, soft, true);

    test.d = 4.0;
    real.d = 2.0;
    soft.i = float64_log2(test.i, &qsf);
    compare(test, real, soft, true);

    test.d = 0x1p64;
    real.d = 64.0;
    soft.i = float64_log2(test.i, &qsf);
    compare(test, real, soft, true);

    test.d = __builtin_inf();
    real.d = __builtin_inf();
    soft.i = float64_log2(test.i, &qsf);
    compare(test, real, soft, true);

    for (i = 0; i < 10000; ++i) {
        test.d = drand48() + 1.0;    /* [1.0, 2.0) */
        real.d = log2(test.d);
        soft.i = float64_log2(test.i, &qsf);
        compare(test, real, soft, false);

        test.d = drand48() * 100;    /* [0.0, 100) */
        real.d = log2(test.d);
        soft.i = float64_log2(test.i, &qsf);
        compare(test, real, soft, false);
    }

    return 0;
}
