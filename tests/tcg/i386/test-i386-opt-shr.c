/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Regression test for tcg optimize vs sign bit repetition counting. */

#include <assert.h>

int main()
{
#ifndef __x86_64__
    char test;

    asm("movw $0x4000, %%ax\n\t"
        "addw %%ax, %%ax\n\t"
        "cwtl\n\t"
        "shrl %%eax\n\t"
        "cmpw $-0x3fff, %%ax\n\t"
        "setnl %%al"
        : "=a"(test));
    assert(!test);
#endif
    return 0;
}
