/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Regression test for tcg optimize vs sign bit repetitions after rotate. */

#include <assert.h>

int main(void)
{
    /* Load from memory so that TCG does not see a constant. */
    volatile int v = 3;
    int x = v;
    char test;

    asm("movsbl %b1, %1\n\t"
        "roll $31, %1\n\t"
        "movsbl %b1, %1\n\t"
        "testl %1, %1\n\t"
        "setg %0"
        : "=q"(test), "+q"(x));
    assert(test);
    return 0;
}
