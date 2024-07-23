/*
 * SME outer product, FZ vs FZ16
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <stdio.h>

static void test_fmopa(uint32_t *result)
{
    asm(".arch_extension sme\n\t"
        "smstart\n\t"               /* Z*, P* and ZArray cleared */
        "ptrue p2.b, vl16\n\t"      /* Limit vector length to 16 */
        "ptrue p5.b, vl16\n\t"
        "movi d0, #0x00ff\n\t"      /* fp16 denormal */
        "movi d16, #0x00ff\n\t"
        "mov w15, #0x0001000000\n\t" /* FZ=1, FZ16=0 */
        "msr fpcr, x15\n\t"
        "fmopa za3.s, p2/m, p5/m, z16.h, z0.h\n\t"
        "mov w15, #0\n\t"
        "st1w {za3h.s[w15, 0]}, p2, [%0]\n\t"
        "add %0, %0, #16\n\t"
        "st1w {za3h.s[w15, 1]}, p2, [%0]\n\t"
        "mov w15, #2\n\t"
        "add %0, %0, #16\n\t"
        "st1w {za3h.s[w15, 0]}, p2, [%0]\n\t"
        "add %0, %0, #16\n\t"
        "st1w {za3h.s[w15, 1]}, p2, [%0]\n\t"
        "smstop"
        : "+r"(result) :
        : "x15", "x16", "p2", "p5", "d0", "d16", "memory");
}

int main(void)
{
    uint32_t result[4 * 4] = { };

    test_fmopa(result);

    if (result[0] != 0x2f7e0100) {
        printf("Test failed: Incorrect output in first 4 bytes\n"
               "Expected: %08x\n"
               "Got:      %08x\n",
               0x2f7e0100, result[0]);
        return 1;
    }

    for (int i = 1; i < 16; ++i) {
        if (result[i] != 0) {
            printf("Test failed: Non-zero word at position %d\n", i);
            return 1;
        }
    }

    return 0;
}
