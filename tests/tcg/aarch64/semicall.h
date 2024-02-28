/*
 * Semihosting Tests - AArch64 helper
 *
 * Copyright (c) 2019, 2024
 * Written by Alex Bennée <alex.bennee@linaro.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

uintptr_t __semi_call(uintptr_t type, uintptr_t arg0)
{
    register uintptr_t t asm("x0") = type;
    register uintptr_t a0 asm("x1") = arg0;
    asm("hlt 0xf000"
        : "=r" (t)
        : "r" (t), "r" (a0));
    return t;
}
