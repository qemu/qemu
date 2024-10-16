/* SPDX-License-Identifier: GPL-2.0-or-later */
/* See https://gitlab.com/qemu-project/qemu/-/issues/2185 */

#include <assert.h>

int test_setc(unsigned int x, unsigned int y)
{
    asm("blsi %1, %0; setc %b0" : "+r"(x) : "r"(y));
    return (unsigned char)x;
}

int test_pushf(unsigned int x, unsigned int y)
{
    asm("blsi %1, %0; pushf; pop %q0" : "+r"(x) : "r"(y));
    return x & 1;
}

int main()
{
    assert(test_setc(1, 0xedbf530a));
    assert(test_pushf(1, 0xedbf530a));
    return 0;
}

