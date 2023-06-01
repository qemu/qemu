/*
 * Test the MXDB and MXDBR instructions.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdlib.h>

int main(void)
{
    union {
        double d[2];
        long double ld;
    } a;
    double b;

    a.d[0] = 1.2345;
    a.d[1] = 999;
    b = 6.789;
    asm("mxdb %[a],%[b]" : [a] "+f" (a.ld) : [b] "R" (b));
    assert(a.ld > 8.38 && a.ld < 8.39);

    a.d[0] = 1.2345;
    a.d[1] = 999;
    b = 6.789;
    asm("mxdbr %[a],%[b]" : [a] "+f" (a.ld) : [b] "f" (b));
    assert(a.ld > 8.38 && a.ld < 8.39);

    return EXIT_SUCCESS;
}
