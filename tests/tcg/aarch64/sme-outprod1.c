/*
 * SME outer product, 1 x 1.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>

extern void foo(float *dst);

asm(
"	.arch_extension sme\n"
"	.type foo, @function\n"
"foo:\n"
"	stp x29, x30, [sp, -80]!\n"
"	mov x29, sp\n"
"	stp d8, d9, [sp, 16]\n"
"	stp d10, d11, [sp, 32]\n"
"	stp d12, d13, [sp, 48]\n"
"	stp d14, d15, [sp, 64]\n"
"	smstart\n"
"	ptrue p0.s, vl4\n"
"	fmov z0.s, #1.0\n"
/*
 * An outer product of a vector of 1.0 by itself should be a matrix of 1.0.
 * Note that we are using tile 1 here (za1.s) rather than tile 0.
 */
"	zero {za}\n"
"	fmopa za1.s, p0/m, p0/m, z0.s, z0.s\n"
/*
 * Read the first 4x4 sub-matrix of elements from tile 1:
 * Note that za1h should be interchangable here.
 */
"	mov w12, #0\n"
"	mova z0.s, p0/m, za1v.s[w12, #0]\n"
"	mova z1.s, p0/m, za1v.s[w12, #1]\n"
"	mova z2.s, p0/m, za1v.s[w12, #2]\n"
"	mova z3.s, p0/m, za1v.s[w12, #3]\n"
/*
 * And store them to the input pointer (dst in the C code):
 */
"	st1w {z0.s}, p0, [x0]\n"
"	add x0, x0, #16\n"
"	st1w {z1.s}, p0, [x0]\n"
"	add x0, x0, #16\n"
"	st1w {z2.s}, p0, [x0]\n"
"	add x0, x0, #16\n"
"	st1w {z3.s}, p0, [x0]\n"
"	smstop\n"
"	ldp d8, d9, [sp, 16]\n"
"	ldp d10, d11, [sp, 32]\n"
"	ldp d12, d13, [sp, 48]\n"
"	ldp d14, d15, [sp, 64]\n"
"	ldp x29, x30, [sp], 80\n"
"	ret\n"
"	.size foo, . - foo"
);

int main()
{
    float dst[16];
    int i, j;

    foo(dst);

    for (i = 0; i < 16; i++) {
        if (dst[i] != 1.0f) {
            break;
        }
    }

    if (i == 16) {
        return 0; /* success */
    }

    /* failure */
    for (i = 0; i < 4; ++i) {
        for (j = 0; j < 4; ++j) {
            printf("%f ", (double)dst[i * 4 + j]);
        }
        printf("\n");
    }
    return 1;
}
