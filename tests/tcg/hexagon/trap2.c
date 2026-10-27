/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <stdio.h>

uint32_t err;

#include "hex_test.h"

static int trap2_getpid(void)
{
    int pid;

    asm volatile("r6 = #172\n" /* __NR_getpid */
                 ".word 0x54c0c004\n" /* trap2(#1) */
                 "%0 = r0\n"
                 : "=r"(pid)
                 :
                 : "r0", "r6");
    return pid;
}

int main(void)
{
    if (trap2_getpid() <= 0) {
        err++;
    }
    puts(err ? "FAIL" : "PASS");
    return err;
}
