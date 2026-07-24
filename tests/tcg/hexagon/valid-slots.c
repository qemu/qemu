/*
 * Regression tests for valid packets that qemu incorrectly rejected as
 * invalid.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdint.h>

int err;

#include "hex_test.h"

/* volatile to keep the load from being optimized away */
static volatile int buf[2] = { 0x1234, 0 };

/* Load and register transfer in one packet, load encoded first. */
static int32_t load_imm_pair(void)
{
    int32_t out;
    /* { r6 = memw(r3+#-4); r7 = #0x4ae6 } */
    asm volatile(
        "{ r3 = %1 }\n\t"
        ".word 0x97837fe6\n\t"
        ".word 0x7845dcc7\n\t"
        "{ %0 = r6 }\n\t"
        : "=r"(out) : "r"(&buf[1]) : "r3", "r6", "r7");
    return out;
}

static int32_t dcbuf[8] __attribute__((aligned(32)));

/* Slot-0-only op (dczeroa) packed last with three transfers. */
static void slot0_restricted(int32_t *out)
{
    asm volatile(
        "{ %0 = #0x11\n\t"
        "  %1 = #0x22\n\t"
        "  %2 = #0x33\n\t"
        "  dczeroa(%3) }\n\t"
        : "=r"(out[0]), "=r"(out[1]), "=r"(out[2])
        : "r"(dcbuf) : "memory");
}

int main()
{
    int32_t r[3];

    check32(load_imm_pair(), 0x1234);

    dcbuf[0] = 0x5a5a5a5a;
    slot0_restricted(r);
    check32(r[0], 0x11);
    check32(r[1], 0x22);
    check32(r[2], 0x33);
    check32(dcbuf[0], 0);       /* dczeroa cleared the line */

    puts(err ? "FAIL" : "PASS");
    return err;
}
