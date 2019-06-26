/*
 *  Test program for MSA instruction PCKEV.H
 *
 *  Copyright (C) 2019  Wave Computing, Inc.
 *  Copyright (C) 2019  Aleksandar Markovic <amarkovic@wavecomp.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *`
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include <sys/time.h>
#include <stdint.h>

#include "../../../../include/wrappers_msa.h"
#include "../../../../include/test_inputs_128.h"
#include "../../../../include/test_utils_128.h"

#define TEST_COUNT_TOTAL (                                                \
            (PATTERN_INPUTS_SHORT_COUNT) * (PATTERN_INPUTS_SHORT_COUNT) + \
            3 * (RANDOM_INPUTS_SHORT_COUNT) * (RANDOM_INPUTS_SHORT_COUNT))


int32_t main(void)
{
    char *isa_ase_name = "MSA";
    char *group_name = "Pack";
    char *instruction_name =  "PCKEV.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0x0000000000000000ULL, 0xffffffffffffffffULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xffffffffffffffffULL, },
        { 0x5555555555555555ULL, 0xffffffffffffffffULL, },
        { 0xccccccccccccccccULL, 0xffffffffffffffffULL, },
        { 0x3333333333333333ULL, 0xffffffffffffffffULL, },
        { 0x8e3838e338e3e38eULL, 0xffffffffffffffffULL, },
        { 0x71c7c71cc71c1c71ULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0x0000000000000000ULL, },
        { 0x5555555555555555ULL, 0x0000000000000000ULL, },
        { 0xccccccccccccccccULL, 0x0000000000000000ULL, },
        { 0x3333333333333333ULL, 0x0000000000000000ULL, },
        { 0x8e3838e338e3e38eULL, 0x0000000000000000ULL, },
        { 0x71c7c71cc71c1c71ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xaaaaaaaaaaaaaaaaULL, },    /*  16  */
        { 0x0000000000000000ULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x5555555555555555ULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xccccccccccccccccULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x3333333333333333ULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x8e3838e338e3e38eULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x71c7c71cc71c1c71ULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xffffffffffffffffULL, 0x5555555555555555ULL, },    /*  24  */
        { 0x0000000000000000ULL, 0x5555555555555555ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xccccccccccccccccULL, 0x5555555555555555ULL, },
        { 0x3333333333333333ULL, 0x5555555555555555ULL, },
        { 0x8e3838e338e3e38eULL, 0x5555555555555555ULL, },
        { 0x71c7c71cc71c1c71ULL, 0x5555555555555555ULL, },
        { 0xffffffffffffffffULL, 0xccccccccccccccccULL, },    /*  32  */
        { 0x0000000000000000ULL, 0xccccccccccccccccULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xccccccccccccccccULL, },
        { 0x5555555555555555ULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x3333333333333333ULL, 0xccccccccccccccccULL, },
        { 0x8e3838e338e3e38eULL, 0xccccccccccccccccULL, },
        { 0x71c7c71cc71c1c71ULL, 0xccccccccccccccccULL, },
        { 0xffffffffffffffffULL, 0x3333333333333333ULL, },    /*  40  */
        { 0x0000000000000000ULL, 0x3333333333333333ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0x3333333333333333ULL, },
        { 0x5555555555555555ULL, 0x3333333333333333ULL, },
        { 0xccccccccccccccccULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x8e3838e338e3e38eULL, 0x3333333333333333ULL, },
        { 0x71c7c71cc71c1c71ULL, 0x3333333333333333ULL, },
        { 0xffffffffffffffffULL, 0x8e3838e338e3e38eULL, },    /*  48  */
        { 0x0000000000000000ULL, 0x8e3838e338e3e38eULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0x8e3838e338e3e38eULL, },
        { 0x5555555555555555ULL, 0x8e3838e338e3e38eULL, },
        { 0xccccccccccccccccULL, 0x8e3838e338e3e38eULL, },
        { 0x3333333333333333ULL, 0x8e3838e338e3e38eULL, },
        { 0x8e3838e338e3e38eULL, 0x8e3838e338e3e38eULL, },
        { 0x71c7c71cc71c1c71ULL, 0x8e3838e338e3e38eULL, },
        { 0xffffffffffffffffULL, 0x71c7c71cc71c1c71ULL, },    /*  56  */
        { 0x0000000000000000ULL, 0x71c7c71cc71c1c71ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0x71c7c71cc71c1c71ULL, },
        { 0x5555555555555555ULL, 0x71c7c71cc71c1c71ULL, },
        { 0xccccccccccccccccULL, 0x71c7c71cc71c1c71ULL, },
        { 0x3333333333333333ULL, 0x71c7c71cc71c1c71ULL, },
        { 0x8e3838e338e3e38eULL, 0x71c7c71cc71c1c71ULL, },
        { 0x71c7c71cc71c1c71ULL, 0x71c7c71cc71c1c71ULL, },
        { 0x0b5eb00ce6cc5540ULL, 0x0b5eb00ce6cc5540ULL, },    /*  64  */
        { 0xbb1a52fc0063c708ULL, 0x0b5eb00ce6cc5540ULL, },
        { 0xc6ff2514aeaa8b80ULL, 0x0b5eb00ce6cc5540ULL, },
        { 0x88d8e2a0164de24eULL, 0x0b5eb00ce6cc5540ULL, },
        { 0x0b5eb00ce6cc5540ULL, 0xbb1a52fc0063c708ULL, },
        { 0xbb1a52fc0063c708ULL, 0xbb1a52fc0063c708ULL, },
        { 0xc6ff2514aeaa8b80ULL, 0xbb1a52fc0063c708ULL, },
        { 0x88d8e2a0164de24eULL, 0xbb1a52fc0063c708ULL, },
        { 0x0b5eb00ce6cc5540ULL, 0xc6ff2514aeaa8b80ULL, },    /*  72  */
        { 0xbb1a52fc0063c708ULL, 0xc6ff2514aeaa8b80ULL, },
        { 0xc6ff2514aeaa8b80ULL, 0xc6ff2514aeaa8b80ULL, },
        { 0x88d8e2a0164de24eULL, 0xc6ff2514aeaa8b80ULL, },
        { 0x0b5eb00ce6cc5540ULL, 0x88d8e2a0164de24eULL, },
        { 0xbb1a52fc0063c708ULL, 0x88d8e2a0164de24eULL, },
        { 0xc6ff2514aeaa8b80ULL, 0x88d8e2a0164de24eULL, },
        { 0x88d8e2a0164de24eULL, 0x88d8e2a0164de24eULL, },
        { 0x0b5eb00ce6cc5540ULL, 0xe2a0e24ee2a0e24eULL, },    /*  80  */
        { 0xbb1a52fc0063c708ULL, 0xe24ee24eb00c5540ULL, },
        { 0xc6ff2514aeaa8b80ULL, 0xe24e554052fcc708ULL, },
        { 0x88d8e2a0164de24eULL, 0x5540c70825148b80ULL, },
        { 0x0b5eb00ce6cc5540ULL, 0xc7088b80e2a0e24eULL, },
        { 0xbb1a52fc0063c708ULL, 0x8b80e24eb00c5540ULL, },
        { 0xc6ff2514aeaa8b80ULL, 0xe24e554052fcc708ULL, },
        { 0x88d8e2a0164de24eULL, 0x5540c70825148b80ULL, },
        { 0x0b5eb00ce6cc5540ULL, 0xc7088b80e2a0e24eULL, },    /*  88  */
        { 0xbb1a52fc0063c708ULL, 0x8b80e24eb00c5540ULL, },
        { 0xc6ff2514aeaa8b80ULL, 0xe24e554052fcc708ULL, },
        { 0x88d8e2a0164de24eULL, 0x5540c70825148b80ULL, },
        { 0x0b5eb00ce6cc5540ULL, 0xc7088b80e2a0e24eULL, },
        { 0xbb1a52fc0063c708ULL, 0x8b80e24eb00c5540ULL, },
        { 0xc6ff2514aeaa8b80ULL, 0xe24e554052fcc708ULL, },
        { 0x88d8e2a0164de24eULL, 0x5540c70825148b80ULL, },
        { 0xc7088b80e2a0e24eULL, 0x0b5eb00ce6cc5540ULL, },    /*  96  */
        { 0xb00c55408b80e24eULL, 0x0b5eb00ce6cc5540ULL, },
        { 0xb00c55405540e24eULL, 0x0b5eb00ce6cc5540ULL, },
        { 0xb00c55405540e24eULL, 0x0b5eb00ce6cc5540ULL, },
        { 0xb00c55405540e24eULL, 0xbb1a52fc0063c708ULL, },
        { 0x52fcc7085540e24eULL, 0xbb1a52fc0063c708ULL, },
        { 0x52fcc708c708e24eULL, 0xbb1a52fc0063c708ULL, },
        { 0x52fcc708c708e24eULL, 0xbb1a52fc0063c708ULL, },
        { 0x52fcc708c708e24eULL, 0xc6ff2514aeaa8b80ULL, },    /* 104  */
        { 0x25148b80c708e24eULL, 0xc6ff2514aeaa8b80ULL, },
        { 0x25148b808b80e24eULL, 0xc6ff2514aeaa8b80ULL, },
        { 0x25148b808b80e24eULL, 0xc6ff2514aeaa8b80ULL, },
        { 0x25148b808b80e24eULL, 0x88d8e2a0164de24eULL, },
        { 0xe2a0e24e8b80e24eULL, 0x88d8e2a0164de24eULL, },
        { 0xe2a0e24ee24ee24eULL, 0x88d8e2a0164de24eULL, },
        { 0xe2a0e24ee24ee24eULL, 0x88d8e2a0164de24eULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_PCKEV_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_PCKEV_H(b128_random[i], b128_random[j],
                           b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                        (PATTERN_INPUTS_SHORT_COUNT)) +
                                       RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_PCKEV_H__DDT(b128_random[i], b128_random[j],
                              b128_result[
                                    ((PATTERN_INPUTS_SHORT_COUNT) *
                                     (PATTERN_INPUTS_SHORT_COUNT)) +
                                    ((RANDOM_INPUTS_SHORT_COUNT) *
                                     (RANDOM_INPUTS_SHORT_COUNT)) +
                                    RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_PCKEV_H__DSD(b128_random[i], b128_random[j],
                                b128_result[
                                    ((PATTERN_INPUTS_SHORT_COUNT) *
                                     (PATTERN_INPUTS_SHORT_COUNT)) +
                                    (2 * (RANDOM_INPUTS_SHORT_COUNT) *
                                     (RANDOM_INPUTS_SHORT_COUNT)) +
                                    RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    gettimeofday(&end, NULL);

    elapsed_time = (end.tv_sec - start.tv_sec) * 1000.0;
    elapsed_time += (end.tv_usec - start.tv_usec) / 1000.0;

    ret = check_results_128(isa_ase_name, group_name, instruction_name,
                            TEST_COUNT_TOTAL, elapsed_time,
                            &b128_result[0][0], &b128_expect[0][0]);

    return ret;
}
