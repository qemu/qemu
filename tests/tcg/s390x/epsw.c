/*
 * Test the EPSW instruction.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdlib.h>

int main(void)
{
    unsigned long r1 = 0x1234567887654321UL, r2 = 0x8765432112345678UL;

    asm("cr %[r1],%[r2]\n"  /* cc = 1 */
        "epsw %[r1],%[r2]"
        : [r1] "+r" (r1), [r2] "+r" (r2) : : "cc");

    /* Do not check the R and RI bits. */
    r1 &= ~0x40000008UL;
    assert(r1 == 0x1234567807051001UL);
    assert(r2 == 0x8765432180000000UL);

    return EXIT_SUCCESS;
}
