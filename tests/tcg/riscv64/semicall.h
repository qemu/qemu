/*
 * Semihosting Tests - RiscV64 Helper
 *
 * Copyright (c) 2021
 * Written by Alex Bennée <alex.bennee@linaro.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

uintptr_t __semi_call(uintptr_t type, uintptr_t arg0)
{
    register uintptr_t t asm("a0") = type;
    register uintptr_t a0 asm("a1") = arg0;
    asm(".option norvc\n\t"
        ".balign 16\n\t"
        "slli zero, zero, 0x1f\n\t"
        "ebreak\n\t"
        "srai zero, zero, 0x7\n\t"
        : "=r" (t)
        : "r" (t), "r" (a0));
    return t;
}
