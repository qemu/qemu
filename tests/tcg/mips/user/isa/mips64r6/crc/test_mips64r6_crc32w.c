/*
 *  Test program for MIPS64R6 instruction CRC32W
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
    char *instruction_name =   "CRC32W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b64_result[TEST_COUNT_TOTAL];
    uint64_t b64_expect[TEST_COUNT_TOTAL] = {
        0x0000000000000000ULL,                    /*   0  */
        0xffffffffdebb20e3ULL,
        0x000000004a691fa1ULL,
        0xffffffff94d23f42ULL,
        0xffffffff8f0808a0ULL,
        0x0000000051b32843ULL,
        0x0000000065069dceULL,
        0xffffffffbbbdbd2dULL,
        0xffffffffdebb20e3ULL,                    /*   8  */
        0x0000000000000000ULL,
        0xffffffff94d23f42ULL,
        0x000000004a691fa1ULL,
        0x0000000051b32843ULL,
        0xffffffff8f0808a0ULL,
        0xffffffffbbbdbd2dULL,
        0x0000000065069dceULL,
        0x000000004a691fa1ULL,                    /*  16  */
        0xffffffff94d23f42ULL,
        0x0000000000000000ULL,
        0xffffffffdebb20e3ULL,
        0xffffffffc5611701ULL,
        0x000000001bda37e2ULL,
        0x000000002f6f826fULL,
        0xfffffffff1d4a28cULL,
        0xffffffff94d23f42ULL,                    /*  24  */
        0x000000004a691fa1ULL,
        0xffffffffdebb20e3ULL,
        0x0000000000000000ULL,
        0x000000001bda37e2ULL,
        0xffffffffc5611701ULL,
        0xfffffffff1d4a28cULL,
        0x000000002f6f826fULL,
        0xffffffff8f0808a0ULL,                    /*  32  */
        0x0000000051b32843ULL,
        0xffffffffc5611701ULL,
        0x000000001bda37e2ULL,
        0x0000000000000000ULL,
        0xffffffffdebb20e3ULL,
        0xffffffffea0e956eULL,
        0x0000000034b5b58dULL,
        0x0000000051b32843ULL,                    /*  40  */
        0xffffffff8f0808a0ULL,
        0x000000001bda37e2ULL,
        0xffffffffc5611701ULL,
        0xffffffffdebb20e3ULL,
        0x0000000000000000ULL,
        0x0000000034b5b58dULL,
        0xffffffffea0e956eULL,
        0x0000000065069dceULL,                    /*  48  */
        0xffffffffbbbdbd2dULL,
        0x000000002f6f826fULL,
        0xfffffffff1d4a28cULL,
        0xffffffffea0e956eULL,
        0x0000000034b5b58dULL,
        0x0000000000000000ULL,
        0xffffffffdebb20e3ULL,
        0xffffffffbbbdbd2dULL,                    /*  56  */
        0x0000000065069dceULL,
        0xfffffffff1d4a28cULL,
        0x000000002f6f826fULL,
        0x0000000034b5b58dULL,
        0xffffffffea0e956eULL,
        0xffffffffdebb20e3ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,                    /*  64  */
        0xffffffff90485967ULL,
        0x000000006dfb974aULL,
        0x00000000083e4538ULL,
        0xffffffff90485967ULL,
        0x0000000000000000ULL,
        0xfffffffffdb3ce2dULL,
        0xffffffff98761c5fULL,
        0x000000006dfb974aULL,                    /*  72  */
        0xfffffffffdb3ce2dULL,
        0x0000000000000000ULL,
        0x0000000065c5d272ULL,
        0x00000000083e4538ULL,
        0xffffffff98761c5fULL,
        0x0000000065c5d272ULL,
        0x0000000000000000ULL,
    };

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_CRC32W(b64_pattern + i, b64_pattern + j,
                b64_result + (PATTERN_INPUTS_64_SHORT_COUNT * i + j));
        }
    }

    for (i = 0; i < RANDOM_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_CRC32W(b64_random + i, b64_random + j,
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
