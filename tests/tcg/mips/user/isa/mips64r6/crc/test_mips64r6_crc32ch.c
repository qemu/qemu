/*
 *  Test program for MIPS64R6 instruction CRC32CH
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
    char *instruction_name =   "CRC32CH";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b64_result[TEST_COUNT_TOTAL];
    uint64_t b64_expect[TEST_COUNT_TOTAL] = {
        0x000000000000ffffULL,                    /*   0  */
        0x000000000e9e77d2ULL,
        0xfffffffff92eaa4bULL,
        0xfffffffff7b02266ULL,
        0x00000000571acc93ULL,
        0x00000000598444beULL,
        0xfffffffff1e6ca77ULL,
        0xffffffffff78425aULL,
        0x000000000e9e882dULL,                    /*   8  */
        0x0000000000000000ULL,
        0xfffffffff7b0dd99ULL,
        0xfffffffff92e55b4ULL,
        0x000000005984bb41ULL,
        0x00000000571a336cULL,
        0xffffffffff78bda5ULL,
        0xfffffffff1e63588ULL,
        0xfffffffff92eff1eULL,                    /*  16  */
        0xfffffffff7b07733ULL,
        0x000000000000aaaaULL,
        0x000000000e9e2287ULL,
        0xffffffffae34cc72ULL,
        0xffffffffa0aa445fULL,
        0x0000000008c8ca96ULL,
        0x00000000065642bbULL,
        0xfffffffff7b088ccULL,                    /*  24  */
        0xfffffffff92e00e1ULL,
        0x000000000e9edd78ULL,
        0x0000000000005555ULL,
        0xffffffffa0aabba0ULL,
        0xffffffffae34338dULL,
        0x000000000656bd44ULL,
        0x0000000008c83569ULL,
        0x00000000571affa0ULL,                    /*  32  */
        0x000000005984778dULL,
        0xffffffffae34aa14ULL,
        0xffffffffa0aa2239ULL,
        0x000000000000ccccULL,
        0x000000000e9e44e1ULL,
        0xffffffffa6fcca28ULL,
        0xffffffffa8624205ULL,
        0x0000000059848872ULL,                    /*  40  */
        0x00000000571a005fULL,
        0xffffffffa0aaddc6ULL,
        0xffffffffae3455ebULL,
        0x000000000e9ebb1eULL,
        0x0000000000003333ULL,
        0xffffffffa862bdfaULL,
        0xffffffffa6fc35d7ULL,
        0xfffffffff1e6bbb0ULL,                    /*  48  */
        0xffffffffff78339dULL,
        0x0000000008c8ee04ULL,
        0x0000000006566629ULL,
        0xffffffffa6fc88dcULL,
        0xffffffffa86200f1ULL,
        0x0000000000008e38ULL,
        0x000000000e9e0615ULL,
        0xffffffffff78cc62ULL,                    /*  56  */
        0xfffffffff1e6444fULL,
        0x00000000065699d6ULL,
        0x0000000008c811fbULL,
        0xffffffffa862ff0eULL,
        0xffffffffa6fc7723ULL,
        0x000000000e9ef9eaULL,
        0x00000000000071c7ULL,
        0x0000000000002862ULL,                    /*  64  */
        0x000000001190c4cfULL,
        0x000000007b7fdbbeULL,
        0xffffffff9204da99ULL,
        0x000000001190a13eULL,
        0x0000000000004d93ULL,
        0x000000006aef52e2ULL,
        0xffffffff839453c5ULL,
        0x000000007b7f4a13ULL,                    /*  72  */
        0x000000006aefa6beULL,
        0x000000000000b9cfULL,
        0xffffffffe97bb8e8ULL,
        0xffffffff9204accaULL,
        0xffffffff83944067ULL,
        0xffffffffe97b5f16ULL,
        0x0000000000005e31ULL,
    };

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_CRC32CH(b64_pattern + i, b64_pattern + j,
                b64_result + (PATTERN_INPUTS_64_SHORT_COUNT * i + j));
        }
    }

    for (i = 0; i < RANDOM_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_CRC32CH(b64_random + i, b64_random + j,
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
