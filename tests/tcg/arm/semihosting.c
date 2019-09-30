/*
 * linux-user semihosting checks
 *
 * Copyright (c) 2019
 * Written by Alex Bennée <alex.bennee@linaro.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>

#define SYS_WRITE0      0x04
#define SYS_REPORTEXC   0x18

void __semi_call(uintptr_t type, uintptr_t arg0)
{
#if defined(__arm__)
    register uintptr_t t asm("r0") = type;
    register uintptr_t a0 asm("r1") = arg0;
    asm("svc 0xab"
        : /* no return */
        : "r" (t), "r" (a0));
#else
    register uintptr_t t asm("x0") = type;
    register uintptr_t a0 asm("x1") = arg0;
    asm("hlt 0xf000"
        : /* no return */
        : "r" (t), "r" (a0));
#endif
}

int main(int argc, char *argv[argc])
{
#if defined(__arm__)
    uintptr_t exit_code = 0x20026;
#else
    uintptr_t exit_block[2] = {0x20026, 0};
    uintptr_t exit_code = (uintptr_t) &exit_block;
#endif

    __semi_call(SYS_WRITE0, (uintptr_t) "Hello World");
    __semi_call(SYS_REPORTEXC, exit_code);
    /* if we get here we failed */
    return -1;
}
