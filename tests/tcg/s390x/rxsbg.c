/*
 * Test the RXSBG instruction.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdlib.h>

static inline __attribute__((__always_inline__)) void
rxsbg(unsigned long *r1, unsigned long r2, int i3, int i4, int i5, int *cc)
{
    asm("rxsbg %[r1],%[r2],%[i3],%[i4],%[i5]\n"
        "ipm %[cc]"
        : [r1] "+r" (*r1), [cc] "=r" (*cc)
        : [r2] "r" (r2) , [i3] "i" (i3) , [i4] "i" (i4) , [i5] "i" (i5)
        : "cc");
    *cc = (*cc >> 28) & 3;
}

void test_cc0(void)
{
    unsigned long r1 = 6;
    int cc;

    rxsbg(&r1, 3, 61 | 0x80, 62, 1, &cc);
    assert(r1 == 6);
    assert(cc == 0);
}

void test_cc1(void)
{
    unsigned long r1 = 2;
    int cc;

    rxsbg(&r1, 3, 61 | 0x80, 62, 1, &cc);
    assert(r1 == 2);
    assert(cc == 1);
}

int main(void)
{
    test_cc0();
    test_cc1();

    return EXIT_SUCCESS;
}
