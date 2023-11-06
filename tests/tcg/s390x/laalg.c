/*
 * Test the LAALG instruction.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdlib.h>

int main(void)
{
    unsigned long cc = 0, op1, op2 = 40, op3 = 2;

    asm("slgfi %[cc],1\n"  /* Set cc_src = -1. */
        "laalg %[op1],%[op3],%[op2]\n"
        "ipm %[cc]"
        : [cc] "+r" (cc)
        , [op1] "=r" (op1)
        , [op2] "+T" (op2)
        : [op3] "r" (op3)
        : "cc");

    assert(cc == 0xffffffff10ffffff);
    assert(op1 == 40);
    assert(op2 == 42);

    return EXIT_SUCCESS;
}
