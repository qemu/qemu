/*
 * SME outer product, [ 1 2 3 4 ] squared
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

static const float i_1234[4] = {
    1.0f, 2.0f, 3.0f, 4.0f
};

static const float expected[4] = {
    4.515625f, 5.750000f, 6.984375f, 8.218750f
};

static void test_fmopa(float *result)
{
    asm(".arch_extension sme\n\t"
        "smstart\n\t"               /* ZArray cleared */
        "ptrue p2.b, vl16\n\t"      /* Limit vector length to 16 */
        "ld1w {z0.s}, p2/z, [%1]\n\t"
        "mov w15, #0\n\t"
        "mov za3h.s[w15, 0], p2/m, z0.s\n\t"
        "mov za3h.s[w15, 1], p2/m, z0.s\n\t"
        "mov w15, #2\n\t"
        "mov za3h.s[w15, 0], p2/m, z0.s\n\t"
        "mov za3h.s[w15, 1], p2/m, z0.s\n\t"
        "msr fpcr, xzr\n\t"
        "fmopa za3.s, p2/m, p2/m, z0.h, z0.h\n\t"
        "mov w15, #0\n\t"
        "st1w {za3h.s[w15, 0]}, p2, [%0]\n"
        "add %0, %0, #16\n\t"
        "st1w {za3h.s[w15, 1]}, p2, [%0]\n\t"
        "mov w15, #2\n\t"
        "add %0, %0, #16\n\t"
        "st1w {za3h.s[w15, 0]}, p2, [%0]\n\t"
        "add %0, %0, #16\n\t"
        "st1w {za3h.s[w15, 1]}, p2, [%0]\n\t"
        "smstop"
        : "+r"(result) : "r"(i_1234)
        : "x15", "x16", "p2", "d0", "memory");
}

int main(void)
{
    float result[4 * 4] = { };
    int ret = 0;

    test_fmopa(result);

    for (int i = 0; i < 4; i++) {
        float actual = result[i];
        if (fabsf(actual - expected[i]) > 0.001f) {
            printf("Test failed at element %d: Expected %f, got %f\n",
                   i, expected[i], actual);
            ret = 1;
        }
    }
    return ret;
}
