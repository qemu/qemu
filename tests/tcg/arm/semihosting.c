/*
 * linux-user semihosting checks
 *
 * Copyright (c) 2019
 * Written by Alex Bennée <alex.bennee@linaro.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>
#include "semicall.h"

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
