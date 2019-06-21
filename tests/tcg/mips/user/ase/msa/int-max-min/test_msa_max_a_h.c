/*
 *  Test program for MSA instruction MAX_A.H
 *
 *  Copyright (C) 2019  Wave Computing, Inc.
 *  Copyright (C) 2019  Aleksandar Markovic <amarkovic@wavecomp.com>
 *  Copyright (C) 2019  RT-RK Computer Based Systems LLC
 *  Copyright (C) 2019  Mateja Marjanovic <mateja.marjanovic@rt-rk.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
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
            (RANDOM_INPUTS_SHORT_COUNT) * (RANDOM_INPUTS_SHORT_COUNT))


int32_t main(void)
{
    char *isa_ase_name = "MSA";
    char *group_name = "Int Max Min";
    char *instruction_name =  "MAX_A.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaa8e38aaaaULL, 0xaaaa8e38aaaaaaaaULL, },
        { 0xaaaaaaaa71c7aaaaULL, 0xaaaa71c7aaaaaaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x555555558e385555ULL, 0x55558e3855555555ULL, },
        { 0x5555555571c75555ULL, 0x555571c755555555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xcccc38e38e38ccccULL, 0x38e38e38cccc38e3ULL, },
        { 0xccccc71c71c7ccccULL, 0xc71c71c7ccccc71cULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x333338e38e383333ULL, 0x38e38e38333338e3ULL, },
        { 0x3333c71c71c73333ULL, 0xc71c71c73333c71cULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xaaaaaaaa8e38aaaaULL, 0xaaaa8e38aaaaaaaaULL, },
        { 0x555555558e385555ULL, 0x55558e3855555555ULL, },
        { 0xcccc38e38e38ccccULL, 0x38e38e38cccc38e3ULL, },
        { 0x333338e38e383333ULL, 0x38e38e38333338e3ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xe38ec71c8e38e38eULL, 0xc71c8e38e38ec71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0xaaaaaaaa71c7aaaaULL, 0xaaaa71c7aaaaaaaaULL, },
        { 0x5555555571c75555ULL, 0x555571c755555555ULL, },
        { 0xccccc71c71c7ccccULL, 0xc71c71c7ccccc71cULL, },
        { 0x3333c71c71c73333ULL, 0xc71c71c73333c71cULL, },
        { 0xe38ec71c8e38e38eULL, 0xc71c8e38e38ec71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x886ae6cc28625540ULL, 0x4b670b5efe7bb00cULL, },    /*  64  */
        { 0x886ae6cc4d935540ULL, 0x4b67bb1a153f52fcULL, },
        { 0x886aaeaab9cf8b80ULL, 0x4b67c6ffab2bb00cULL, },
        { 0x886ae6cc5e315540ULL, 0x8df188d8a942b00cULL, },
        { 0x886ae6cc4d935540ULL, 0x4b67bb1a153f52fcULL, },
        { 0xfbbe00634d93c708ULL, 0x12f7bb1a153f52fcULL, },
        { 0xac5aaeaa4d938b80ULL, 0x27d8bb1aab2b52fcULL, },
        { 0x704f164d5e31c708ULL, 0x8df188d8a94252fcULL, },
        { 0x886aaeaab9cf8b80ULL, 0x4b67c6ffab2bb00cULL, },    /*  72  */
        { 0xac5aaeaa4d938b80ULL, 0x27d8bb1aab2b52fcULL, },
        { 0xac5aaeaab9cf8b80ULL, 0x27d8c6ffab2b2514ULL, },
        { 0x704faeaa5e318b80ULL, 0x8df188d8a9422514ULL, },
        { 0x886ae6cc5e315540ULL, 0x8df188d8a942b00cULL, },
        { 0x704f164d5e31c708ULL, 0x8df188d8a94252fcULL, },
        { 0x704faeaa5e318b80ULL, 0x8df188d8a9422514ULL, },
        { 0x704f164d5e31e24eULL, 0x8df188d8a942e2a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MAX_A_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MAX_A_H(b128_random[i], b128_random[j],
                           b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                        (PATTERN_INPUTS_SHORT_COUNT)) +
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
