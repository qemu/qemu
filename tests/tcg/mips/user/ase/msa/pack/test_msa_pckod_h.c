/*
 *  Test program for MSA instruction PCKOD.H
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
    char *instruction_name =  "PCKOD.H";
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
        { 0x38e3e38ee38e8e38ULL, 0xffffffffffffffffULL, },
        { 0xc71c1c711c7171c7ULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0x0000000000000000ULL, },
        { 0x5555555555555555ULL, 0x0000000000000000ULL, },
        { 0xccccccccccccccccULL, 0x0000000000000000ULL, },
        { 0x3333333333333333ULL, 0x0000000000000000ULL, },
        { 0x38e3e38ee38e8e38ULL, 0x0000000000000000ULL, },
        { 0xc71c1c711c7171c7ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xaaaaaaaaaaaaaaaaULL, },    /*  16  */
        { 0x0000000000000000ULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x5555555555555555ULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xccccccccccccccccULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x3333333333333333ULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x38e3e38ee38e8e38ULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xc71c1c711c7171c7ULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xffffffffffffffffULL, 0x5555555555555555ULL, },    /*  24  */
        { 0x0000000000000000ULL, 0x5555555555555555ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xccccccccccccccccULL, 0x5555555555555555ULL, },
        { 0x3333333333333333ULL, 0x5555555555555555ULL, },
        { 0x38e3e38ee38e8e38ULL, 0x5555555555555555ULL, },
        { 0xc71c1c711c7171c7ULL, 0x5555555555555555ULL, },
        { 0xffffffffffffffffULL, 0xccccccccccccccccULL, },    /*  32  */
        { 0x0000000000000000ULL, 0xccccccccccccccccULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xccccccccccccccccULL, },
        { 0x5555555555555555ULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x3333333333333333ULL, 0xccccccccccccccccULL, },
        { 0x38e3e38ee38e8e38ULL, 0xccccccccccccccccULL, },
        { 0xc71c1c711c7171c7ULL, 0xccccccccccccccccULL, },
        { 0xffffffffffffffffULL, 0x3333333333333333ULL, },    /*  40  */
        { 0x0000000000000000ULL, 0x3333333333333333ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0x3333333333333333ULL, },
        { 0x5555555555555555ULL, 0x3333333333333333ULL, },
        { 0xccccccccccccccccULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x38e3e38ee38e8e38ULL, 0x3333333333333333ULL, },
        { 0xc71c1c711c7171c7ULL, 0x3333333333333333ULL, },
        { 0xffffffffffffffffULL, 0x38e3e38ee38e8e38ULL, },    /*  48  */
        { 0x0000000000000000ULL, 0x38e3e38ee38e8e38ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0x38e3e38ee38e8e38ULL, },
        { 0x5555555555555555ULL, 0x38e3e38ee38e8e38ULL, },
        { 0xccccccccccccccccULL, 0x38e3e38ee38e8e38ULL, },
        { 0x3333333333333333ULL, 0x38e3e38ee38e8e38ULL, },
        { 0x38e3e38ee38e8e38ULL, 0x38e3e38ee38e8e38ULL, },
        { 0xc71c1c711c7171c7ULL, 0x38e3e38ee38e8e38ULL, },
        { 0xffffffffffffffffULL, 0xc71c1c711c7171c7ULL, },    /*  56  */
        { 0x0000000000000000ULL, 0xc71c1c711c7171c7ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xc71c1c711c7171c7ULL, },
        { 0x5555555555555555ULL, 0xc71c1c711c7171c7ULL, },
        { 0xccccccccccccccccULL, 0xc71c1c711c7171c7ULL, },
        { 0x3333333333333333ULL, 0xc71c1c711c7171c7ULL, },
        { 0x38e3e38ee38e8e38ULL, 0xc71c1c711c7171c7ULL, },
        { 0xc71c1c711c7171c7ULL, 0xc71c1c711c7171c7ULL, },
        { 0x4b67fe7b886a2862ULL, 0x4b67fe7b886a2862ULL, },    /*  64  */
        { 0x12f7153ffbbe4d93ULL, 0x4b67fe7b886a2862ULL, },
        { 0x27d8ab2bac5ab9cfULL, 0x4b67fe7b886a2862ULL, },
        { 0x8df1a942704f5e31ULL, 0x4b67fe7b886a2862ULL, },
        { 0x4b67fe7b886a2862ULL, 0x12f7153ffbbe4d93ULL, },
        { 0x12f7153ffbbe4d93ULL, 0x12f7153ffbbe4d93ULL, },
        { 0x27d8ab2bac5ab9cfULL, 0x12f7153ffbbe4d93ULL, },
        { 0x8df1a942704f5e31ULL, 0x12f7153ffbbe4d93ULL, },
        { 0x4b67fe7b886a2862ULL, 0x27d8ab2bac5ab9cfULL, },    /*  72  */
        { 0x12f7153ffbbe4d93ULL, 0x27d8ab2bac5ab9cfULL, },
        { 0x27d8ab2bac5ab9cfULL, 0x27d8ab2bac5ab9cfULL, },
        { 0x8df1a942704f5e31ULL, 0x27d8ab2bac5ab9cfULL, },
        { 0x4b67fe7b886a2862ULL, 0x8df1a942704f5e31ULL, },
        { 0x12f7153ffbbe4d93ULL, 0x8df1a942704f5e31ULL, },
        { 0x27d8ab2bac5ab9cfULL, 0x8df1a942704f5e31ULL, },
        { 0x8df1a942704f5e31ULL, 0x8df1a942704f5e31ULL, },
        { 0x4b67fe7b886a2862ULL, 0x8df1704f8df1704fULL, },    /*  80  */
        { 0x12f7153ffbbe4d93ULL, 0x8df18df14b67886aULL, },
        { 0x27d8ab2bac5ab9cfULL, 0x8df14b6712f7fbbeULL, },
        { 0x8df1a942704f5e31ULL, 0x8df112f727d8ac5aULL, },
        { 0x4b67fe7b886a2862ULL, 0x8df127d88df1704fULL, },
        { 0x12f7153ffbbe4d93ULL, 0x8df18df14b67886aULL, },
        { 0x27d8ab2bac5ab9cfULL, 0x8df14b6712f7fbbeULL, },
        { 0x8df1a942704f5e31ULL, 0x8df112f727d8ac5aULL, },
        { 0x4b67fe7b886a2862ULL, 0x8df127d88df1704fULL, },    /*  88  */
        { 0x12f7153ffbbe4d93ULL, 0x8df18df14b67886aULL, },
        { 0x27d8ab2bac5ab9cfULL, 0x8df14b6712f7fbbeULL, },
        { 0x8df1a942704f5e31ULL, 0x8df112f727d8ac5aULL, },
        { 0x4b67fe7b886a2862ULL, 0x8df127d88df1704fULL, },
        { 0x12f7153ffbbe4d93ULL, 0x8df18df14b67886aULL, },
        { 0x27d8ab2bac5ab9cfULL, 0x8df14b6712f7fbbeULL, },
        { 0x8df1a942704f5e31ULL, 0x8df112f727d8ac5aULL, },
        { 0x8df127d88df1704fULL, 0x4b67fe7b886a2862ULL, },    /*  96  */
        { 0x4b67886a8df18df1ULL, 0x4b67fe7b886a2862ULL, },
        { 0x4b67886a4b678df1ULL, 0x4b67fe7b886a2862ULL, },
        { 0x4b67886a4b674b67ULL, 0x4b67fe7b886a2862ULL, },
        { 0x4b67886a4b674b67ULL, 0x12f7153ffbbe4d93ULL, },
        { 0x12f7fbbe4b674b67ULL, 0x12f7153ffbbe4d93ULL, },
        { 0x12f7fbbe12f74b67ULL, 0x12f7153ffbbe4d93ULL, },
        { 0x12f7fbbe12f712f7ULL, 0x12f7153ffbbe4d93ULL, },
        { 0x12f7fbbe12f712f7ULL, 0x27d8ab2bac5ab9cfULL, },    /* 104  */
        { 0x27d8ac5a12f712f7ULL, 0x27d8ab2bac5ab9cfULL, },
        { 0x27d8ac5a27d812f7ULL, 0x27d8ab2bac5ab9cfULL, },
        { 0x27d8ac5a27d827d8ULL, 0x27d8ab2bac5ab9cfULL, },
        { 0x27d8ac5a27d827d8ULL, 0x8df1a942704f5e31ULL, },
        { 0x8df1704f27d827d8ULL, 0x8df1a942704f5e31ULL, },
        { 0x8df1704f8df127d8ULL, 0x8df1a942704f5e31ULL, },
        { 0x8df1704f8df18df1ULL, 0x8df1a942704f5e31ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_PCKOD_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_PCKOD_H(b128_random[i], b128_random[j],
                           b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                        (PATTERN_INPUTS_SHORT_COUNT)) +
                                       RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_PCKOD_H__DDT(b128_random[i], b128_random[j],
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
            do_msa_PCKOD_H__DSD(b128_random[i], b128_random[j],
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
