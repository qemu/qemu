/*
 * Fused Multiply Add (Single)
 *
 * Copyright (c) 2019 Linaro
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
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


static void print_inputs(float a, float b, float c)
{
    char *a_fmt, *b_fmt, *c_fmt;

    a_fmt = fmt_f32(a);
    b_fmt = fmt_f32(b);
    c_fmt = fmt_f32(c);

    printf("op : %s * %s + %s\n", a_fmt, b_fmt, c_fmt);

    free(a_fmt);
    free(b_fmt);
    free(c_fmt);
}

static void print_result(float r, int j, int k)
{
    char *r_fmt, *flag_fmt;

    flag_fmt = fmt_flags();
    r_fmt = fmt_f32(r);

    printf("res: %s flags=%s (%d/%d)\n", r_fmt, flag_fmt, j, k);

    free(r_fmt);
    free(flag_fmt);
}

static void do_madds(float a, float b, float c, int j, int k)
{
    float r;

    print_inputs(a, b, c);

    feclearexcept(FE_ALL_EXCEPT);
    r = __builtin_fmaf(a, b, c);

    print_result(r, j, k);
}

int main(int argc, char *argv[argc])
{
    int i, j, k, nums = get_num_f32();
    float a, b, c;

    for (i = 0; i < ARRAY_SIZE(round_flags); ++i) {
        if (fesetround(round_flags[i].flag) != 0) {
            printf("### Rounding %s skipped\n", round_flags[i].desc);
            continue;
        }
        printf("### Rounding %s\n", round_flags[i].desc);
        for (j = 0; j < nums; j++) {
            for (k = 0; k < 3; k++) {
                a = get_f32(j + ((k)%3));
                b = get_f32(j + ((k+1)%3));
                c = get_f32(j + ((k+2)%3));
                do_madds(a, b, c, j, k);
            }
        }

        /* From https://bugs.launchpad.net/qemu/+bug/1841491 */
        printf("# LP184149\n");
        do_madds(0x1.ffffffffffffcp-1022, 0x1.0000000000001p-1, 0x0.0000000000001p-1022, j, 0);
        do_madds(0x8p-152, 0x8p-152, 0x8p-152, j+1, 0);
    }

    return 0;
}
