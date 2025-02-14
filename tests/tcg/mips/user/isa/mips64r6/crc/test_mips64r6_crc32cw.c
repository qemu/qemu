/*
 *  Test program for MIPS64R6 instruction CRC32CW
 *
 *  Copyright (C) 2019  Wave Computing, Inc.
 *  Copyright (C) 2019  Aleksandar Markovic <amarkovic@wavecomp.com>
 *  Copyright (C) 2025  Aleksandar Rakic <aleksandar.rakic@htecgroup.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <sys/time.h>
#include <stdint.h>

#include "../../../../include/wrappers_mips64r6.h"
#include "../../../../include/test_inputs_64.h"
#include "../../../../include/test_utils_64.h"

#define TEST_COUNT_TOTAL (PATTERN_INPUTS_64_COUNT + RANDOM_INPUTS_64_COUNT)

int32_t main(void)
{
    char *isa_ase_name = "mips64r6";
    char *group_name = "CRC with reversed polynomial 0x82F63B78";
    char *instruction_name =   "CRC32CW";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b64_result[TEST_COUNT_TOTAL];
    uint64_t b64_expect[TEST_COUNT_TOTAL] = {
        0x0000000000000000ULL,                    /*   0  */
        0xffffffffb798b438ULL,
        0xffffffff91d3be47ULL,
        0x00000000264b0a7fULL,
        0x0000000070b16a3dULL,
        0xffffffffc729de05ULL,
        0x0000000063c5950aULL,
        0xffffffffd45d2132ULL,
        0xffffffffb798b438ULL,                    /*   8  */
        0x0000000000000000ULL,
        0x00000000264b0a7fULL,
        0xffffffff91d3be47ULL,
        0xffffffffc729de05ULL,
        0x0000000070b16a3dULL,
        0xffffffffd45d2132ULL,
        0x0000000063c5950aULL,
        0xffffffff91d3be47ULL,                    /*  16  */
        0x00000000264b0a7fULL,
        0x0000000000000000ULL,
        0xffffffffb798b438ULL,
        0xffffffffe162d47aULL,
        0x0000000056fa6042ULL,
        0xfffffffff2162b4dULL,
        0x00000000458e9f75ULL,
        0x00000000264b0a7fULL,                    /*  24  */
        0xffffffff91d3be47ULL,
        0xffffffffb798b438ULL,
        0x0000000000000000ULL,
        0x0000000056fa6042ULL,
        0xffffffffe162d47aULL,
        0x00000000458e9f75ULL,
        0xfffffffff2162b4dULL,
        0x0000000070b16a3dULL,                    /*  32  */
        0xffffffffc729de05ULL,
        0xffffffffe162d47aULL,
        0x0000000056fa6042ULL,
        0x0000000000000000ULL,
        0xffffffffb798b438ULL,
        0x000000001374ff37ULL,
        0xffffffffa4ec4b0fULL,
        0xffffffffc729de05ULL,                    /*  40  */
        0x0000000070b16a3dULL,
        0x0000000056fa6042ULL,
        0xffffffffe162d47aULL,
        0xffffffffb798b438ULL,
        0x0000000000000000ULL,
        0xffffffffa4ec4b0fULL,
        0x000000001374ff37ULL,
        0x0000000063c5950aULL,                    /*  48  */
        0xffffffffd45d2132ULL,
        0xfffffffff2162b4dULL,
        0x00000000458e9f75ULL,
        0x000000001374ff37ULL,
        0xffffffffa4ec4b0fULL,
        0x0000000000000000ULL,
        0xffffffffb798b438ULL,
        0xffffffffd45d2132ULL,                    /*  56  */
        0x0000000063c5950aULL,
        0x00000000458e9f75ULL,
        0xfffffffff2162b4dULL,
        0xffffffffa4ec4b0fULL,
        0x000000001374ff37ULL,
        0xffffffffb798b438ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,                    /*  64  */
        0xffffffffea0755b2ULL,
        0x0000000008b188e6ULL,
        0xffffffffff3cc8d9ULL,
        0xffffffffea0755b2ULL,
        0x0000000000000000ULL,
        0xffffffffe2b6dd54ULL,
        0x00000000153b9d6bULL,
        0x0000000008b188e6ULL,                    /*  72  */
        0xffffffffe2b6dd54ULL,
        0x0000000000000000ULL,
        0xfffffffff78d403fULL,
        0xffffffffff3cc8d9ULL,
        0x00000000153b9d6bULL,
        0xfffffffff78d403fULL,
        0x0000000000000000ULL,
    };

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_CRC32CW(b64_pattern + i, b64_pattern + j,
                b64_result + (PATTERN_INPUTS_64_SHORT_COUNT * i + j));
        }
    }

    for (i = 0; i < RANDOM_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_CRC32CW(b64_random + i, b64_random + j,
                b64_result + (((PATTERN_INPUTS_64_SHORT_COUNT) *
                               (PATTERN_INPUTS_64_SHORT_COUNT)) +
                              RANDOM_INPUTS_64_SHORT_COUNT * i + j));
        }
    }

    gettimeofday(&end, NULL);

    elapsed_time = (end.tv_sec - start.tv_sec) * 1000.0;
    elapsed_time += (end.tv_usec - start.tv_usec) / 1000.0;

    ret = check_results_64(isa_ase_name, group_name, instruction_name,
                           TEST_COUNT_TOTAL, elapsed_time, b64_result,
                           b64_expect);

    return ret;
}
