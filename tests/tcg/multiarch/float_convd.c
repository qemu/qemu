/*
 * Floating Point Convert Doubles to Various
 *
 * Copyright (c) 2019, 2024 Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>
#include <fenv.h>


#include "float_helpers.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

typedef struct {
    int flag;
    char *desc;
} float_mapping;

float_mapping round_flags[] = {
    { FE_TONEAREST, "to nearest" },
#ifdef FE_UPWARD
    { FE_UPWARD, "upwards" },
#endif
#ifdef FE_DOWNWARD
    { FE_DOWNWARD, "downwards" },
#endif
#ifdef FE_TOWARDZERO
    { FE_TOWARDZERO, "to zero" }
#endif
};

static void print_input(double input)
{
    char *in_fmt = fmt_f64(input);
    printf("from double: %s\n", in_fmt);
    free(in_fmt);
}

static void convert_double_to_single(double input)
{
    float output;
    char *out_fmt, *flag_fmt;

    feclearexcept(FE_ALL_EXCEPT);

    output = input;

    flag_fmt = fmt_flags();
    out_fmt = fmt_f32(output);
    printf("  to single: %s (%s)\n", out_fmt, flag_fmt);
    free(out_fmt);
    free(flag_fmt);
}

#define xstr(a) str(a)
#define str(a) #a

#define CONVERT_DOUBLE_TO_INT(TYPE, FMT)                            \
    static void convert_double_to_ ## TYPE(double input)            \
    {                                                               \
        TYPE ## _t output;                                          \
        char *flag_fmt;                                             \
        const char to[] = "to " xstr(TYPE);                         \
        feclearexcept(FE_ALL_EXCEPT);                               \
        output = input;                                             \
        flag_fmt = fmt_flags();                                     \
        printf("%11s: %" FMT " (%s)\n", to, output, flag_fmt);      \
        free(flag_fmt);                                             \
    }

CONVERT_DOUBLE_TO_INT( int32, PRId32)
CONVERT_DOUBLE_TO_INT(uint32, PRId32)
CONVERT_DOUBLE_TO_INT( int64, PRId64)
CONVERT_DOUBLE_TO_INT(uint64, PRId64)

int main(int argc, char *argv[argc])
{
    int i, j, nums;

    nums = get_num_f64();

    for (i = 0; i < ARRAY_SIZE(round_flags); ++i) {
        if (fesetround(round_flags[i].flag) != 0) {
            printf("### Rounding %s skipped\n", round_flags[i].desc);
            continue;
        }
        printf("### Rounding %s\n", round_flags[i].desc);
        for (j = 0; j < nums; j++) {
            double input = get_f64(j);
            print_input(input);
            convert_double_to_single(input);
            convert_double_to_int32(input);
            convert_double_to_int64(input);
            convert_double_to_uint32(input);
            convert_double_to_uint64(input);
        }
    }

    return 0;
}
