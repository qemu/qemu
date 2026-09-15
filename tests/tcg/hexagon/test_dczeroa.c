/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Test the dczeroa instruction, which zeroes a 32-byte cache line.
 * The address is rounded down to a 32-byte boundary: (addr & ~0x1f).
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <hexagon_protos.h>

int err;

#include "hex_test.h"

static uint8_t buf[128] __attribute__((aligned(32)));

/* dczeroa with an aligned address */
static void test_dczeroa_aligned(void)
{
    int i;

    memset(buf, 0xff, sizeof(buf));

    Q6_dczeroa_A(&buf[32]);

    for (i = 0; i < 32; i++) {
        check32(buf[i], 0xff);
    }
    for (i = 32; i < 64; i++) {
        check32(buf[i], 0x00);
    }
    for (i = 64; i < 96; i++) {
        check32(buf[i], 0xff);
    }
}

/* dczeroa with an unaligned address (should round down) */
static void test_dczeroa_unaligned(void)
{
    int i;

    memset(buf, 0xff, sizeof(buf));

    /* Address buf+40 rounds down to buf+32 */
    Q6_dczeroa_A(&buf[40]);

    for (i = 0; i < 32; i++) {
        check32(buf[i], 0xff);
    }
    for (i = 32; i < 64; i++) {
        check32(buf[i], 0x00);
    }
    for (i = 64; i < 96; i++) {
        check32(buf[i], 0xff);
    }
}

/* dczeroa at the start of the buffer */
static void test_dczeroa_start(void)
{
    int i;

    memset(buf, 0xff, sizeof(buf));

    Q6_dczeroa_A(&buf[0]);

    for (i = 0; i < 32; i++) {
        check32(buf[i], 0x00);
    }
    for (i = 32; i < 64; i++) {
        check32(buf[i], 0xff);
    }
}

int main(void)
{
    test_dczeroa_aligned();
    test_dczeroa_unaligned();
    test_dczeroa_start();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
