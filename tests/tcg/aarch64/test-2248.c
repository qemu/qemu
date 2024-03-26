/* SPDX-License-Identifier: GPL-2.0-or-later */
/* See https://gitlab.com/qemu-project/qemu/-/issues/2248 */

#include <assert.h>

__attribute__((noinline))
long test(long x, long y, long sh)
{
    long r;
    asm("cmp   %1, %2\n\t"
        "cset  x12, lt\n\t"
        "and   w11, w12, #0xff\n\t"
        "cmp   w11, #0\n\t"
        "csetm x14, ne\n\t"
        "lsr   x13, x14, %3\n\t"
        "sxtb  %0, w13"
        : "=r"(r)
        : "r"(x), "r"(y), "r"(sh)
        : "x11", "x12", "x13", "x14");
    return r;
}

int main()
{
    long r = test(0, 1, 2);
    assert(r == -1);
    return 0;
}
