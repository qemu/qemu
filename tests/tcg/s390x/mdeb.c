/*
 * Test the MDEB and MDEBR instructions.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdlib.h>

int main(void)
{
    union {
        float f[2];
        double d;
    } a;
    float b;

    a.f[0] = 1.2345;
    a.f[1] = 999;
    b = 6.789;
    asm("mdeb %[a],%[b]" : [a] "+f" (a.d) : [b] "R" (b));
    assert(a.d > 8.38 && a.d < 8.39);

    a.f[0] = 1.2345;
    a.f[1] = 999;
    b = 6.789;
    asm("mdebr %[a],%[b]" : [a] "+f" (a.d) : [b] "f" (b));
    assert(a.d > 8.38 && a.d < 8.39);

    return EXIT_SUCCESS;
}
