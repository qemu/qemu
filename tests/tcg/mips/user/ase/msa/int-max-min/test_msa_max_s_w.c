/*
 *  Test program for MSA instruction MAX_S.W
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
    char *instruction_name =  "MAX_S.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xffffffffffffffffULL, 0x38e38e38ffffffffULL, },
        { 0x1c71c71c71c71c71ULL, 0xffffffff1c71c71cULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x0000000000000000ULL, 0x38e38e3800000000ULL, },
        { 0x1c71c71c71c71c71ULL, 0x000000001c71c71cULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  16  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xe38e38e3aaaaaaaaULL, 0x38e38e38e38e38e3ULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555571c71c71ULL, 0x5555555555555555ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  32  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xe38e38e3ccccccccULL, 0x38e38e38e38e38e3ULL, },
        { 0x1c71c71c71c71c71ULL, 0xcccccccc1c71c71cULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x38e38e3833333333ULL, },
        { 0x3333333371c71c71ULL, 0x3333333333333333ULL, },
        { 0xffffffffffffffffULL, 0x38e38e38ffffffffULL, },    /*  48  */
        { 0x0000000000000000ULL, 0x38e38e3800000000ULL, },
        { 0xe38e38e3aaaaaaaaULL, 0x38e38e38e38e38e3ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xe38e38e3ccccccccULL, 0x38e38e38e38e38e3ULL, },
        { 0x3333333333333333ULL, 0x38e38e3833333333ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x1c71c71c71c71c71ULL, 0x38e38e381c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xffffffff1c71c71cULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0x000000001c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x5555555571c71c71ULL, 0x5555555555555555ULL, },
        { 0x1c71c71c71c71c71ULL, 0xcccccccc1c71c71cULL, },
        { 0x3333333371c71c71ULL, 0x3333333333333333ULL, },
        { 0x1c71c71c71c71c71ULL, 0x38e38e381c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x886ae6cc28625540ULL, 0x4b670b5efe7bb00cULL, },    /*  64  */
        { 0xfbbe00634d93c708ULL, 0x4b670b5e153f52fcULL, },
        { 0xac5aaeaa28625540ULL, 0x4b670b5efe7bb00cULL, },
        { 0x704f164d5e31e24eULL, 0x4b670b5efe7bb00cULL, },
        { 0xfbbe00634d93c708ULL, 0x4b670b5e153f52fcULL, },
        { 0xfbbe00634d93c708ULL, 0x12f7bb1a153f52fcULL, },
        { 0xfbbe00634d93c708ULL, 0x27d8c6ff153f52fcULL, },
        { 0x704f164d5e31e24eULL, 0x12f7bb1a153f52fcULL, },
        { 0xac5aaeaa28625540ULL, 0x4b670b5efe7bb00cULL, },    /*  72  */
        { 0xfbbe00634d93c708ULL, 0x27d8c6ff153f52fcULL, },
        { 0xac5aaeaab9cf8b80ULL, 0x27d8c6ffab2b2514ULL, },
        { 0x704f164d5e31e24eULL, 0x27d8c6ffab2b2514ULL, },
        { 0x704f164d5e31e24eULL, 0x4b670b5efe7bb00cULL, },
        { 0x704f164d5e31e24eULL, 0x12f7bb1a153f52fcULL, },
        { 0x704f164d5e31e24eULL, 0x27d8c6ffab2b2514ULL, },
        { 0x704f164d5e31e24eULL, 0x8df188d8a942e2a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MAX_S_W(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MAX_S_W(b128_random[i], b128_random[j],
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
