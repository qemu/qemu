/*
 * Test the v83 scalar two-lane floating-point instructions.
 *
 * The assembler does not yet know these instructions, so use their encodings.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <stdio.h>

uint32_t err;

#include "hex_test.h"

static uint64_t vsfadd(uint64_t a, uint64_t b)
{
    uint64_t result;

    asm volatile("r3:2 = %1\n"
                 "r5:4 = %2\n"
                 ".word 0xeb02c440\n" /* r1:0 = vsfadd(r3:2, r5:4) */
                 "%0 = r1:0\n"
                 : "=r"(result) : "r"(a), "r"(b) : "r0", "r1", "r2",
                   "r3", "r4", "r5");
    return result;
}

static uint64_t vsfsub(uint64_t a, uint64_t b)
{
    uint64_t result;

    asm volatile("r3:2 = %1\n"
                 "r5:4 = %2\n"
                 ".word 0xeb02c460\n" /* r1:0 = vsfsub(r3:2, r5:4) */
                 "%0 = r1:0\n"
                 : "=r"(result) : "r"(a), "r"(b) : "r0", "r1", "r2",
                   "r3", "r4", "r5");
    return result;
}

static uint64_t vsfmpy(uint64_t a, uint64_t b)
{
    uint64_t result;

    asm volatile("r3:2 = %1\n"
                 "r5:4 = %2\n"
                 ".word 0xeb42c420\n" /* r1:0 = vsfmpy(r3:2, r5:4) */
                 "%0 = r1:0\n"
                 : "=r"(result) : "r"(a), "r"(b) : "r0", "r1", "r2",
                   "r3", "r4", "r5");
    return result;
}

static uint64_t vsffma(uint64_t a, uint64_t b, uint64_t acc)
{
    uint64_t result;

    asm volatile("r1:0 = %3\n"
                 "r3:2 = %1\n"
                 "r5:4 = %2\n"
                 ".word 0xeb42c440\n" /* r1:0 += vsfmpy(r3:2, r5:4) */
                 "%0 = r1:0\n"
                 : "=r"(result) : "r"(a), "r"(b), "r"(acc) : "r0", "r1",
                   "r2", "r3", "r4", "r5");
    return result;
}

static uint64_t vsffms(uint64_t a, uint64_t b, uint64_t acc)
{
    uint64_t result;

    asm volatile("r1:0 = %3\n"
                 "r3:2 = %1\n"
                 "r5:4 = %2\n"
                 ".word 0xeb42c460\n" /* r1:0 -= vsfmpy(r3:2, r5:4) */
                 "%0 = r1:0\n"
                 : "=r"(result) : "r"(a), "r"(b), "r"(acc) : "r0", "r1",
                   "r2", "r3", "r4", "r5");
    return result;
}

int main(void)
{
    uint64_t a = 0x400000003fc00000ULL;
    uint64_t b = 0x4080000040000000ULL;
    uint64_t acc = 0x40a000003f800000ULL;

    check64(vsfadd(a, b), 0x40c0000040600000ULL);
    check64(vsfsub(a, b), 0xc0000000bf000000ULL);
    check64(vsfmpy(a, b), 0x4100000040400000ULL);
    check64(vsffma(a, b, acc), 0x4150000040800000ULL);
    check64(vsffms(a, b, acc), 0xc0400000c0000000ULL);

    puts(err ? "FAIL" : "PASS");
    return err;
}
