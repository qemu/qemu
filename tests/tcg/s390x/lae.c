/*
 * Test the LOAD ADDRESS EXTENDED instruction.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdlib.h>

int main(void)
{
    unsigned long long ar = -1, b2 = 100000, r, x2 = 500;
    /*
     * Hardcode the register number, since clang does not allow using %rN in
     * place of %aN.
     */
    register unsigned long long r2 __asm__("2");
    int tmp;

    asm("ear %[tmp],%%a2\n"
        "lae %%r2,42(%[x2],%[b2])\n"
        "ear %[ar],%%a2\n"
        "sar %%a2,%[tmp]"
        : [tmp] "=&r" (tmp), "=&r" (r2), [ar] "+r" (ar)
        : [b2] "r" (b2), [x2] "r" (x2)
        : "memory");
    r = r2;
    assert(ar == 0xffffffff00000000ULL);
    assert(r == 100542);

    return EXIT_SUCCESS;
}
