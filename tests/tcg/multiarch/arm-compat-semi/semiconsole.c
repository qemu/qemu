/*
 * linux-user semihosting console
 *
 * Copyright (c) 2019
 * Written by Alex Bennée <alex.bennee@linaro.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define SYS_READC       0x07

#include <stdio.h>
#include <stdint.h>
#include "semicall.h"

int main(void)
{
    char c;

    printf("Semihosting Console Test\n");
    printf("hit X to exit:");

    do {
        c = __semi_call(SYS_READC, 0);
        printf("got '%c'\n", c);
    } while (c != 'X');

    return 0;
}
