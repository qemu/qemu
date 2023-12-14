/*
 * Test the LOCFHR instruction.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdlib.h>

static inline __attribute__((__always_inline__)) long
locfhr(long r1, long r2, int m3, int cc)
{
    cc <<= 28;
    asm("spm %[cc]\n"
        "locfhr %[r1],%[r2],%[m3]\n"
        : [r1] "+r" (r1)
        : [cc] "r" (cc), [r2] "r" (r2), [m3] "i" (m3)
        : "cc");
    return r1;
}

int main(void)
{
    assert(locfhr(0x1111111122222222, 0x3333333344444444, 8, 0) ==
           0x3333333322222222);
    assert(locfhr(0x5555555566666666, 0x7777777788888888, 11, 1) ==
           0x5555555566666666);

    return EXIT_SUCCESS;
}
