/*
 *  Test program for MIPS64R6 instruction CRC32CD
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
    char *instruction_name =   "CRC32CD";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b64_result[TEST_COUNT_TOTAL];
    uint64_t b64_expect[TEST_COUNT_TOTAL] = {
        0xffffffffb798b438ULL,                    /*   0  */
        0xffffffffc44ff94dULL,
        0xffffffff992a70ebULL,
        0xffffffffeafd3d9eULL,
        0x000000005152da26ULL,
        0x0000000022859753ULL,
        0x0000000015cb6d32ULL,
        0x00000000661c2047ULL,
        0x0000000073d74d75ULL,                    /*   8  */
        0x0000000000000000ULL,
        0x000000005d6589a6ULL,
        0x000000002eb2c4d3ULL,
        0xffffffff951d236bULL,
        0xffffffffe6ca6e1eULL,
        0xffffffffd184947fULL,
        0xffffffffa253d90aULL,
        0x0000000008f9ceacULL,                    /*  16  */
        0x000000007b2e83d9ULL,
        0x00000000264b0a7fULL,
        0x00000000559c470aULL,
        0xffffffffee33a0b2ULL,
        0xffffffff9de4edc7ULL,
        0xffffffffaaaa17a6ULL,
        0xffffffffd97d5ad3ULL,
        0xffffffffccb637e1ULL,                    /*  24  */
        0xffffffffbf617a94ULL,
        0xffffffffe204f332ULL,
        0xffffffff91d3be47ULL,
        0x000000002a7c59ffULL,
        0x0000000059ab148aULL,
        0x000000006ee5eeebULL,
        0x000000001d32a39eULL,
        0x0000000021e3b01bULL,                    /*  32  */
        0x000000005234fd6eULL,
        0x000000000f5174c8ULL,
        0x000000007c8639bdULL,
        0xffffffffc729de05ULL,
        0xffffffffb4fe9370ULL,
        0xffffffff83b06911ULL,
        0xfffffffff0672464ULL,
        0xffffffffe5ac4956ULL,                    /*  40  */
        0xffffffff967b0423ULL,
        0xffffffffcb1e8d85ULL,
        0xffffffffb8c9c0f0ULL,
        0x0000000003662748ULL,
        0x0000000070b16a3dULL,
        0x0000000047ff905cULL,
        0x000000003428dd29ULL,
        0xffffffffb89d59a6ULL,                    /*  48  */
        0xffffffffcb4a14d3ULL,
        0xffffffff962f9d75ULL,
        0xffffffffe5f8d000ULL,
        0x000000005e5737b8ULL,
        0x000000002d807acdULL,
        0x000000001ace80acULL,
        0x000000006919cdd9ULL,
        0x000000007cd2a0ebULL,                    /*  56  */
        0x000000000f05ed9eULL,
        0x0000000052606438ULL,
        0x0000000021b7294dULL,
        0xffffffff9a18cef5ULL,
        0xffffffffe9cf8380ULL,
        0xffffffffde8179e1ULL,
        0xffffffffad563494ULL,
        0x000000003a358bb3ULL,                    /*  64  */
        0xffffffff975446ebULL,
        0x0000000041d37ad6ULL,
        0x000000004be84fe1ULL,
        0xffffffff9671b1b3ULL,
        0x000000003b107cebULL,
        0xffffffffed9740d6ULL,
        0xffffffffe7ac75e1ULL,
        0xffffffffa1489696ULL,                    /*  72  */
        0x000000000c295bceULL,
        0xffffffffdaae67f3ULL,
        0xffffffffd09552c4ULL,
        0x0000000042bd7071ULL,
        0xffffffffefdcbd29ULL,
        0x00000000395b8114ULL,
        0x000000003360b423ULL,
    };

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_CRC32CD(b64_pattern + i, b64_pattern + j,
                b64_result + (PATTERN_INPUTS_64_SHORT_COUNT * i + j));
        }
    }

    for (i = 0; i < RANDOM_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_CRC32CD(b64_random + i, b64_random + j,
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
