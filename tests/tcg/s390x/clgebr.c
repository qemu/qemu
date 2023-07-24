/*
 * Test the CLGEBR instruction.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <fenv.h>
#include <stdlib.h>

int main(void)
{
    float r2 = -1;
    long long r1;
    int cc;

    feclearexcept(FE_ALL_EXCEPT);
    asm("clgebr %[r1],%[m3],%[r2],%[m4]\n"
        "ipm %[cc]\n"
        : [r1] "=r" (r1)
        , [cc] "=r" (cc)
        : [m3] "i" (5) /* round toward 0 */
        , [r2] "f" (r2)
        , [m4] "i" (8) /* bit 0 is set, but must be ignored; XxC is not set */
        : "cc");
    cc >>= 28;

    assert(r1 == 0);
    assert(cc == 3);
    assert(fetestexcept(FE_ALL_EXCEPT) == (FE_INVALID | FE_INEXACT));

    return EXIT_SUCCESS;
}
