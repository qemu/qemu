/*
 * Test the LOAD REVERSED and STORE REVERSED instructions.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdlib.h>

int main(void)
{
    long i = 0x123456789abcdefULL, o;

    o = i;
    asm("lrvr %0,%1" : "+r" (o) : "r" (i));
    assert(o == 0x1234567efcdab89ULL);

    o = i;
    asm("lrvgr %0,%1" : "+r" (o) : "r" (i));
    assert(o == 0xefcdab8967452301ULL);

    o = i;
    asm("lrvh %0,%1" : "=r" (o) : "T" (i));
    assert(o == 0x123456789ab2301ULL);

    o = i;
    asm("lrv %0,%1" : "=r" (o) : "T" (i));
    assert(o == 0x123456767452301ULL);

    o = i;
    asm("lrvg %0,%1" : "=r" (o) : "T" (i));
    assert(o == 0xefcdab8967452301ULL);

    o = i;
    asm("strvh %1,%0" : "=T" (o) : "r" (i));
    assert(o == 0xefcd456789abcdefULL);

    o = i;
    asm("strv %1,%0" : "=T" (o) : "r" (i));
    assert(o == 0xefcdab8989abcdefULL);

    o = i;
    asm("strvg %1,%0" : "=T" (o) : "r" (i));
    assert(o == 0xefcdab8967452301ULL);

    return EXIT_SUCCESS;
}
