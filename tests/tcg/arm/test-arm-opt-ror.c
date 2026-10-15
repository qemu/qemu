/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Regression test for tcg optimize vs sign bit repetitions after rotate. */

#include <assert.h>

int main(void)
{
    /* Load from memory so that TCG does not see a constant. */
    volatile int v = 3;
    int x = v, gt;

    asm("sxtb %1, %1\n\t"
        "ror %1, %1, #1\n\t"
        "sxtb %1, %1\n\t"
        "cmp %1, #0\n\t"
        "movle %0, #0\n\t"
        "movgt %0, #1"
        : "=r"(gt), "+r"(x) : : "cc");
    assert(gt);
    return 0;
}
