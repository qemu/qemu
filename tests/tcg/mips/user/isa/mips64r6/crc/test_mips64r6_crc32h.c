/*
 *  Test program for MIPS64R6 instruction CRC32H
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
    char *group_name = "CRC with reversed polynomial 0xEDB88320";
    char *instruction_name =   "CRC32H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b64_result[TEST_COUNT_TOTAL];
    uint64_t b64_expect[TEST_COUNT_TOTAL] = {
        0x000000000000ffffULL,                    /*   0  */
        0xffffffffbe2612ffULL,
        0xffffffffdccda6c0ULL,
        0x0000000062eb4bc0ULL,
        0x000000004bbbc8eaULL,
        0xfffffffff59d25eaULL,
        0x0000000022259ac0ULL,
        0xffffffff9c0377c0ULL,
        0xffffffffbe26ed00ULL,                    /*   8  */
        0x0000000000000000ULL,
        0x0000000062ebb43fULL,
        0xffffffffdccd593fULL,
        0xfffffffff59dda15ULL,
        0x000000004bbb3715ULL,
        0xffffffff9c03883fULL,
        0x000000002225653fULL,
        0xffffffffdccdf395ULL,                    /*  16  */
        0x0000000062eb1e95ULL,
        0x000000000000aaaaULL,
        0xffffffffbe2647aaULL,
        0xffffffff9776c480ULL,
        0x0000000029502980ULL,
        0xfffffffffee896aaULL,
        0x0000000040ce7baaULL,
        0x0000000062ebe16aULL,                    /*  24  */
        0xffffffffdccd0c6aULL,
        0xffffffffbe26b855ULL,
        0x0000000000005555ULL,
        0x000000002950d67fULL,
        0xffffffff97763b7fULL,
        0x0000000040ce8455ULL,
        0xfffffffffee86955ULL,
        0x000000004bbbfbd9ULL,                    /*  32  */
        0xfffffffff59d16d9ULL,
        0xffffffff9776a2e6ULL,
        0x0000000029504fe6ULL,
        0x000000000000ccccULL,
        0xffffffffbe2621ccULL,
        0x00000000699e9ee6ULL,
        0xffffffffd7b873e6ULL,
        0xfffffffff59de926ULL,                    /*  40  */
        0x000000004bbb0426ULL,
        0x000000002950b019ULL,
        0xffffffff97765d19ULL,
        0xffffffffbe26de33ULL,
        0x0000000000003333ULL,
        0xffffffffd7b88c19ULL,
        0x00000000699e6119ULL,
        0x000000002225eb07ULL,                    /*  48  */
        0xffffffff9c030607ULL,
        0xfffffffffee8b238ULL,
        0x0000000040ce5f38ULL,
        0x00000000699edc12ULL,
        0xffffffffd7b83112ULL,
        0x0000000000008e38ULL,
        0xffffffffbe266338ULL,
        0xffffffff9c03f9f8ULL,                    /*  56  */
        0x00000000222514f8ULL,
        0x0000000040cea0c7ULL,
        0xfffffffffee84dc7ULL,
        0xffffffffd7b8ceedULL,
        0x00000000699e23edULL,
        0xffffffffbe269cc7ULL,
        0x00000000000071c7ULL,
        0x0000000000002862ULL,                    /*  64  */
        0x0000000026a17af6ULL,
        0xffffffffaa919152ULL,
        0xffffffffcb865590ULL,
        0x0000000026a11f07ULL,
        0x0000000000004d93ULL,
        0xffffffff8c30a637ULL,
        0xffffffffed2762f5ULL,
        0xffffffffaa9100ffULL,                    /*  72  */
        0xffffffff8c30526bULL,
        0x000000000000b9cfULL,
        0x0000000061177d0dULL,
        0xffffffffcb8623c3ULL,
        0xffffffffed277157ULL,
        0x0000000061179af3ULL,
        0x0000000000005e31ULL
    };

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_CRC32H(b64_pattern + i, b64_pattern + j,
                b64_result + (PATTERN_INPUTS_64_SHORT_COUNT * i + j));
        }
    }

    for (i = 0; i < RANDOM_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_CRC32H(b64_random + i, b64_random + j,
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
