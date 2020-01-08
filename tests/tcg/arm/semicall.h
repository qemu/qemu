/*
 * Semihosting Tests
 *
 * Copyright (c) 2019
 * Written by Alex Bennée <alex.bennee@linaro.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define SYS_WRITE0      0x04
#define SYS_READC       0x07
#define SYS_REPORTEXC   0x18

uintptr_t __semi_call(uintptr_t type, uintptr_t arg0)
{
#if defined(__arm__)
    register uintptr_t t asm("r0") = type;
    register uintptr_t a0 asm("r1") = arg0;
#ifdef __thumb__
#  define SVC  "svc 0xab"
#else
#  define SVC  "svc 0x123456"
#endif
    asm(SVC : "=r" (t)
        : "r" (t), "r" (a0));
#else
    register uintptr_t t asm("x0") = type;
    register uintptr_t a0 asm("x1") = arg0;
    asm("hlt 0xf000"
        : "=r" (t)
        : "r" (t), "r" (a0));
#endif

    return t;
}
