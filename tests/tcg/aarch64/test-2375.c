/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2024 Linaro Ltd */
/* See https://gitlab.com/qemu-project/qemu/-/issues/2375 */

#include <assert.h>

int main(void)
{
   int r, z;

   asm("msr fpcr, %2\n\t"
       "fjcvtzs %w0, %d3\n\t"
       "cset %1, eq"
       : "=r"(r), "=r"(z)
       : "r"(0x01000000L),      /* FZ = 1 */
         "w"(0xfcff00L));       /* denormal */

    assert(r == 0);
    assert(z == 0);
    return 0;
}
