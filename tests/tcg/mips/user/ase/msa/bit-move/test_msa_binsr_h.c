/*
 *  Test program for MSA instruction BINSR.H
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
    char *group_name = "Bit Move";
    char *instruction_name =  "BINSR.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x186ac6cc71c21c70ULL, 0xc7670b5e1e7bd00cULL, },    /*  64  */
        { 0x086ac6cc71c21d40ULL, 0xc7670b5efe7bd00cULL, },
        { 0x086ac6cc28621d40ULL, 0xc7670b5efe7bd00cULL, },
        { 0x886ae6cc28625540ULL, 0xc7670b5efe7bd00cULL, },
        { 0x8bbee06328635540ULL, 0xc7f73b1af53fd2fcULL, },
        { 0xfbbee06328635508ULL, 0xc7f73b1a153fd2fcULL, },
        { 0xfbbee0634d935508ULL, 0xc6f7bb1a153fd2fcULL, },
        { 0xfbbec0634d934708ULL, 0xc6f7bb1a153fd2fcULL, },
        { 0xfc5aceaa4d974708ULL, 0xc6d8c6ff1b2bc514ULL, },    /*  72  */
        { 0xac5aceaa4d9f4780ULL, 0xc6d8c6ffab2bc514ULL, },
        { 0xac5aceaab9cf4780ULL, 0xc7d8c6ffab2bc514ULL, },
        { 0xac5aeeaab9cf0b80ULL, 0xc7d8c6ffab2bc514ULL, },
        { 0xa84ff64db9c90b80ULL, 0xc7f188d8a942c2a0ULL, },
        { 0xf04ff64db9c10a4eULL, 0xc7f188d8a942c2a0ULL, },
        { 0xf04ff64d5e310a4eULL, 0xc7f188d8a942c2a0ULL, },
        { 0x704fd64d5e31624eULL, 0xc7f188d8a942c2a0ULL, },
        { 0x704fd64d5e31624eULL, 0xc7f188d8a942c2a0ULL, },    /*  80  */
        { 0x704fd64d5e31624eULL, 0xc7f188d8a942c2a0ULL, },
        { 0x704fd64d5e31624eULL, 0xc7f188d8a942c2a0ULL, },
        { 0x704fd64d5e31624eULL, 0xc7f188d8a942c2a0ULL, },
        { 0x704fd64d5e31624eULL, 0xc7f188d8a942c2a0ULL, },
        { 0x704fd64d5e31624eULL, 0xc7f188d8a942c2a0ULL, },
        { 0x704fd64d5e31624eULL, 0xc7f188d8a942c2a0ULL, },
        { 0x704fd64d5e31624eULL, 0xc7f188d8a942c2a0ULL, },
        { 0x704fd64d5e31624eULL, 0xc7f188d8a942c2a0ULL, },    /*  88  */
        { 0x704fd64d5e31624eULL, 0xc7f188d8a942c2a0ULL, },
        { 0x704fd64d5e31624eULL, 0xc7f188d8a942c2a0ULL, },
        { 0x704fd64d5e31624eULL, 0xc7f188d8a942c2a0ULL, },
        { 0x704fd64d5e31624eULL, 0xc7f188d8a942c2a0ULL, },
        { 0x704fd64d5e31624eULL, 0xc7f188d8a942c2a0ULL, },
        { 0x704fd64d5e31624eULL, 0xc7f188d8a942c2a0ULL, },
        { 0x704fd64d5e31624eULL, 0xc7f188d8a942c2a0ULL, },
        { 0x886ae6cc5e325540ULL, 0xc7f3895ea943c2a0ULL, },    /*  96  */
        { 0x886ae6cc5e325540ULL, 0xc7f78b5ea94bc2a0ULL, },
        { 0x886ae6cc5e325540ULL, 0xc7678b5eae7bc2a0ULL, },
        { 0x886ae6cc5e325540ULL, 0xc7678b5eae7bc2a0ULL, },
        { 0x8bbee0635e335540ULL, 0xc7f7bb1aa53fc2a0ULL, },
        { 0xfbbee0635e335540ULL, 0xc7f7bb1a153fc2a0ULL, },
        { 0xfbbee0635e335540ULL, 0xc7f7bb1a153fc2a0ULL, },
        { 0xfbbee0635e335540ULL, 0xc7f7bb1a153fc2a0ULL, },
        { 0xac5ae06a5e3f5540ULL, 0xc7d8beffab2bc2a0ULL, },    /* 104  */
        { 0xac5ae6aab9cf5540ULL, 0xc7d8c6ffab2bc2a0ULL, },
        { 0xac5ae6aab9cf5540ULL, 0xc7d8c6ffab2bc2a0ULL, },
        { 0xac5ae6aab9cf5540ULL, 0xc7d8c6ffab2bc2a0ULL, },
        { 0xa84fe64d5e315540ULL, 0xc7f188d8a942c2a0ULL, },
        { 0x704fd64d5e315540ULL, 0xc7f188d8a942c2a0ULL, },
        { 0x704fd64d5e315540ULL, 0xc7f188d8a942c2a0ULL, },
        { 0x704fd64d5e315540ULL, 0xc7f188d8a942c2a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_BINSR_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_BINSR_H(b128_random[i], b128_random[j],
                           b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                        (PATTERN_INPUTS_SHORT_COUNT)) +
                                       RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_BINSR_H__DDT(b128_random[i], b128_random[j],
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
            do_msa_BINSR_H__DSD(b128_random[i], b128_random[j],
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
