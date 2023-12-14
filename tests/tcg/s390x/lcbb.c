/*
 * Test the LCBB instruction.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdlib.h>

static inline __attribute__((__always_inline__)) void
lcbb(long *r1, void *dxb2, int m3, int *cc)
{
    asm("lcbb %[r1],%[dxb2],%[m3]\n"
        "ipm %[cc]"
        : [r1] "+r" (*r1), [cc] "=r" (*cc)
        : [dxb2] "R" (*(char *)dxb2), [m3] "i" (m3)
        : "cc");
    *cc = (*cc >> 28) & 3;
}

static char buf[0x1000] __attribute__((aligned(0x1000)));

static inline __attribute__((__always_inline__)) void
test_lcbb(void *p, int m3, int exp_r1, int exp_cc)
{
    long r1 = 0xfedcba9876543210;
    int cc;

    lcbb(&r1, p, m3, &cc);
    assert(r1 == (0xfedcba9800000000 | exp_r1));
    assert(cc == exp_cc);
}

int main(void)
{
    test_lcbb(&buf[0],    0, 16, 0);
    test_lcbb(&buf[63],   0,  1, 3);
    test_lcbb(&buf[0],    1, 16, 0);
    test_lcbb(&buf[127],  1,  1, 3);
    test_lcbb(&buf[0],    2, 16, 0);
    test_lcbb(&buf[255],  2,  1, 3);
    test_lcbb(&buf[0],    3, 16, 0);
    test_lcbb(&buf[511],  3,  1, 3);
    test_lcbb(&buf[0],    4, 16, 0);
    test_lcbb(&buf[1023], 4,  1, 3);
    test_lcbb(&buf[0],    5, 16, 0);
    test_lcbb(&buf[2047], 5,  1, 3);
    test_lcbb(&buf[0],    6, 16, 0);
    test_lcbb(&buf[4095], 6,  1, 3);

    return EXIT_SUCCESS;
}
