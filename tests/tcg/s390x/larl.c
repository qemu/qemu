/*
 * Test the LARL instruction.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdlib.h>

int main(void)
{
    long algfi = (long)main;
    long larl;

    /*
     * The compiler may emit larl for the C addition, so compute the expected
     * value using algfi.
     */
    asm("algfi %[r],0xd0000000" : [r] "+r" (algfi) : : "cc");
    asm("larl %[r],main+0xd0000000" : [r] "=r" (larl));

    return algfi == larl ? EXIT_SUCCESS : EXIT_FAILURE;
}
