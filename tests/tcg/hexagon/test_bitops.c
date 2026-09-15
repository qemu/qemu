/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Test bit interleave/deinterleave and convergent rounding.
 */

#include <stdio.h>
#include <stdint.h>
#include <hexagon_protos.h>

int err;

#include "hex_test.h"

static void check_interleave(void)
{
    uint64_t input;
    uint64_t inter;
    uint64_t deinter;

    /*
     * interleave takes the 32 even bits and 32 odd bits of Rss and
     * interleaves them: even[0] goes to bit 0, odd[0] to bit 1,
     * even[1] to bit 2, odd[1] to bit 3, etc.
     *
     * Input:  low 32 bits (even half), high 32 bits (odd half)
     * If low = 0xFFFFFFFF, high = 0x00000000:
     *   result has every even bit set, every odd bit clear
     *   = 0x5555555555555555
     */
    input = 0x00000000FFFFFFFFULL;
    inter = Q6_P_interleave_P(input);
    check64(inter, 0x5555555555555555ULL);

    /* All ones should stay all ones */
    input = 0xFFFFFFFFFFFFFFFFULL;
    inter = Q6_P_interleave_P(input);
    check64(inter, 0xFFFFFFFFFFFFFFFFULL);

    /* All zeros should stay all zeros */
    input = 0x0000000000000000ULL;
    inter = Q6_P_interleave_P(input);
    check64(inter, 0x0000000000000000ULL);

    /*
     * deinterleave is the inverse of interleave.
     * deinterleave(interleave(x)) should equal x.
     */
    input = 0xDEADBEEFCAFEBABEULL;
    inter = Q6_P_interleave_P(input);
    deinter = Q6_P_deinterleave_P(inter);
    check64(deinter, input);

    /* deinterleave of 0x5555...5555 -> low half all 1s, high half all 0s */
    deinter = Q6_P_deinterleave_P(0x5555555555555555ULL);
    check64(deinter, 0x00000000FFFFFFFFULL);
}

static void check_cround(void)
{
    /*
     * cround(0x300, #8) -> 3 (exact shift, no rounding needed).
     * 0x300 >> 8 = 3
     */
    check32(Q6_R_cround_RI(0x300, 8), 3);

    /*
     * cround(0x380, #8) -> 4 (0x380 >> 8 = 3.5, rounds to even = 4)
     */
    check32(Q6_R_cround_RI(0x380, 8), 4);

    /*
     * cround(0x280, #8) -> 2 (0x280 >> 8 = 2.5, rounds to even = 2)
     */
    check32(Q6_R_cround_RI(0x280, 8), 2);

    /*
     * cround(0x3C0, #8) -> 4 (0x3C0 >> 8 = 3.75, rounds up to 4)
     */
    check32(Q6_R_cround_RI(0x3C0, 8), 4);

    /*
     * Shift by 0 -> return the input unchanged.
     */
    check32(Q6_R_cround_RI(42, 0), 42);
}

int main(void)
{
    check_interleave();
    check_cround();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
