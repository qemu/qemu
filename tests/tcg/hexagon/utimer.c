/*
 * Copyright(c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdint.h>
#include <stdio.h>

static int err;

#include "hex_test.h"

static uint64_t get_time()
{
    uint64_t time;
    asm volatile("%0 = utimer\n\t"
                 : "=r"(time)
                 :
                 :
                 );
    return time;
}

static uint64_t get_time_from_regs()
{
    uint32_t time_low;
    uint32_t time_high;
    asm volatile("%0 = utimerhi\n\t"
                 "%1 = utimerlo\n\t"
                 : "=r"(time_high), "=r"(time_low)
                 :
                 :
                 );
    return ((uint64_t)time_high << 32) | (uint64_t)time_low;
}


int main()
{
    err = 0;

    uint64_t t0 = get_time();
    check64_ne(t0, 0);

    uint64_t t1 = get_time_from_regs();
    check64_ne(t1, 0);

    puts(err ? "FAIL" : "PASS");
    return err;
}
