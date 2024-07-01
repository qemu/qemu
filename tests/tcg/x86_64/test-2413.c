/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2024 Linaro, Ltd. */
/* See https://gitlab.com/qemu-project/qemu/-/issues/2413 */

#include <assert.h>

void test(unsigned long *a, unsigned long *d, unsigned long c)
{
    asm("xorl %%eax, %%eax\n\t"
        "xorl %%edx, %%edx\n\t"
        "testb $0x20, %%cl\n\t"
        "sete %%al\n\t"
        "setne %%dl\n\t"
        "shll %%cl, %%eax\n\t"
        "shll %%cl, %%edx\n\t"
        : "=a"(*a), "=d"(*d)
        : "c"(c));
}

int main(void)
{
    unsigned long a, c, d;

    for (c = 0; c < 64; c++) {
        test(&a, &d, c);
        assert(a == (c & 0x20 ? 0 : 1u << (c & 0x1f)));
        assert(d == (c & 0x20 ? 1u << (c & 0x1f) : 0));
    }
    return 0;
}
