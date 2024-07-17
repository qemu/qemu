/*
 * SME outer product, 1 x 1.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>

static void foo(float *dst)
{
    asm(".arch_extension sme\n\t"
        "smstart\n\t"
        "ptrue p0.s, vl4\n\t"
        "fmov z0.s, #1.0\n\t"
        /*
         * An outer product of a vector of 1.0 by itself should be a matrix of 1.0.
         * Note that we are using tile 1 here (za1.s) rather than tile 0.
         */
        "zero {za}\n\t"
        "fmopa za1.s, p0/m, p0/m, z0.s, z0.s\n\t"
        /*
         * Read the first 4x4 sub-matrix of elements from tile 1:
         * Note that za1h should be interchangeable here.
         */
        "mov w12, #0\n\t"
        "mova z0.s, p0/m, za1v.s[w12, #0]\n\t"
        "mova z1.s, p0/m, za1v.s[w12, #1]\n\t"
        "mova z2.s, p0/m, za1v.s[w12, #2]\n\t"
        "mova z3.s, p0/m, za1v.s[w12, #3]\n\t"
        /*
         * And store them to the input pointer (dst in the C code):
         */
        "st1w {z0.s}, p0, [%0]\n\t"
        "add x0, x0, #16\n\t"
        "st1w {z1.s}, p0, [x0]\n\t"
        "add x0, x0, #16\n\t"
        "st1w {z2.s}, p0, [x0]\n\t"
        "add x0, x0, #16\n\t"
        "st1w {z3.s}, p0, [x0]\n\t"
        "smstop"
        : : "r"(dst)
        : "x12", "d0", "d1", "d2", "d3", "memory");
}

int main()
{
    float dst[16] = { };

    foo(dst);

    for (int i = 0; i < 16; i++) {
        if (dst[i] != 1.0f) {
            goto failure;
        }
    }
    /* success */
    return 0;

 failure:
    for (int i = 0; i < 16; i++) {
        printf("%f%c", dst[i], i % 4 == 3 ? '\n' : ' ');
    }
    return 1;
}
