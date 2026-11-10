/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ENTER's 16-bit size operand is unsigned.  If the decoder sign-extends
 * it, the stack pointer moves in the wrong direction.
 */
#include <stdio.h>

static int check_enter(void)
{
    unsigned long before, after;

#ifdef __x86_64__
    __asm__ volatile(
        "movq %%rsp, %0\n\t"
        "enter $0x8000, $0\n\t"
        "movq %%rsp, %1\n\t"
        "leave\n\t"
        : "=r"(before), "=r"(after)
        :
        : "memory");
    return (before - after) == 0x8008 ? 0 : 1;
#else
    __asm__ volatile(
        "movl %%esp, %0\n\t"
        "enter $0x8000, $0\n\t"
        "movl %%esp, %1\n\t"
        "leave\n\t"
        : "=r"(before), "=r"(after)
        :
        : "memory");
    return (before - after) == 0x8004 ? 0 : 1;
#endif
}

int main(void)
{
    if (check_enter()) {
        printf("FAIL: ENTER treated the 16-bit size operand as signed\n");
        return 1;
    }
    return 0;
}
