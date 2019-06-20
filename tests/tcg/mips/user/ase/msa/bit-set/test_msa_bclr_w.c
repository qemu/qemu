/*
 *  Test program for MSA instruction BCLR.W
 *
 *  Copyright (C) 2019  Wave Computing, Inc.
 *  Copyright (C) 2019  Aleksandar Markovic <amarkovic@wavecomp.com>
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
    char *group_name = "Bit Set";
    char *instruction_name =  "BCLR.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x7fffffff7fffffffULL, 0x7fffffff7fffffffULL, },    /*   0  */
        { 0xfffffffefffffffeULL, 0xfffffffefffffffeULL, },
        { 0xfffffbfffffffbffULL, 0xfffffbfffffffbffULL, },
        { 0xffdfffffffdfffffULL, 0xffdfffffffdfffffULL, },
        { 0xffffefffffffefffULL, 0xffffefffffffefffULL, },
        { 0xfff7fffffff7ffffULL, 0xfff7fffffff7ffffULL, },
        { 0xfffffff7ffffbfffULL, 0xfefffffffffffff7ULL, },
        { 0xeffffffffffdffffULL, 0xffffff7fefffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x2aaaaaaa2aaaaaaaULL, 0x2aaaaaaa2aaaaaaaULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaa8aaaaaaa8aaaaaULL, 0xaa8aaaaaaa8aaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaa2aaaaaaa2aaaaULL, 0xaaa2aaaaaaa2aaaaULL, },
        { 0xaaaaaaa2aaaaaaaaULL, 0xaaaaaaaaaaaaaaa2ULL, },
        { 0xaaaaaaaaaaa8aaaaULL, 0xaaaaaa2aaaaaaaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },    /*  24  */
        { 0x5555555455555554ULL, 0x5555555455555554ULL, },
        { 0x5555515555555155ULL, 0x5555515555555155ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555455555554555ULL, 0x5555455555554555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555551555ULL, 0x5455555555555555ULL, },
        { 0x4555555555555555ULL, 0x5555555545555555ULL, },
        { 0x4ccccccc4cccccccULL, 0x4ccccccc4cccccccULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccc8ccccccc8ccULL, 0xccccc8ccccccc8ccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccc4ccccccc4ccccULL, 0xccc4ccccccc4ccccULL, },
        { 0xccccccc4cccc8cccULL, 0xccccccccccccccc4ULL, },
        { 0xccccccccccccccccULL, 0xcccccc4cccccccccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },    /*  40  */
        { 0x3333333233333332ULL, 0x3333333233333332ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3313333333133333ULL, 0x3313333333133333ULL, },
        { 0x3333233333332333ULL, 0x3333233333332333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3233333333333333ULL, },
        { 0x2333333333313333ULL, 0x3333333323333333ULL, },
        { 0x638e38e30e38e38eULL, 0x38e38e38638e38e3ULL, },    /*  48  */
        { 0xe38e38e28e38e38eULL, 0x38e38e38e38e38e2ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38a38e38e38e3ULL, },
        { 0xe38e38e38e18e38eULL, 0x38c38e38e38e38e3ULL, },
        { 0xe38e28e38e38e38eULL, 0x38e38e38e38e28e3ULL, },
        { 0xe38638e38e30e38eULL, 0x38e38e38e38638e3ULL, },
        { 0xe38e38e38e38a38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x1c71c71c71c71c71ULL, 0x471c71c71c71c71cULL, },    /*  56  */
        { 0x1c71c71c71c71c70ULL, 0xc71c71c61c71c71cULL, },
        { 0x1c71c31c71c71871ULL, 0xc71c71c71c71c31cULL, },
        { 0x1c51c71c71c71c71ULL, 0xc71c71c71c51c71cULL, },
        { 0x1c71c71c71c70c71ULL, 0xc71c61c71c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71471c71c71c71cULL, },
        { 0x1c71c71471c71c71ULL, 0xc61c71c71c71c714ULL, },
        { 0x0c71c71c71c51c71ULL, 0xc71c71470c71c71cULL, },
        { 0x886ae6cc28625540ULL, 0x0b670b5efe7ba00cULL, },    /*  64  */
        { 0x886ae6c428625440ULL, 0x4b670b5eee7bb00cULL, },
        { 0x886ae2cc28625540ULL, 0x4b670b5efe6bb00cULL, },
        { 0x886ac6cc28621540ULL, 0x4a670b5efe7bb00cULL, },
        { 0xfbbe00634d93c708ULL, 0x12f7bb1a153f42fcULL, },
        { 0xfbbe00634d93c608ULL, 0x12f7bb1a053f52fcULL, },
        { 0xfbbe00634d93c708ULL, 0x12f7bb1a152f52fcULL, },
        { 0xfbbe00634d938708ULL, 0x12f7bb1a153f52fcULL, },
        { 0xac5aaeaab9cf8b80ULL, 0x27d8c6ffab2b2514ULL, },    /*  72  */
        { 0xac5aaea2b9cf8a80ULL, 0x23d8c6ffab2b2514ULL, },
        { 0xac5aaaaab9cf8b80ULL, 0x27d8c6ffab2b2514ULL, },
        { 0xac5a8eaab9cf8b80ULL, 0x26d8c6ffab2b2514ULL, },
        { 0x704f064d5e31e24eULL, 0x8df188d8a942e2a0ULL, },
        { 0x704f16455e31e24eULL, 0x89f188d8a942e2a0ULL, },
        { 0x704f124d5e31e24eULL, 0x0df188d8a942e2a0ULL, },
        { 0x704f164d5e31a24eULL, 0x8cf188d8a942e2a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_BCLR_W(b128_pattern[i], b128_pattern[j],
                          b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_BCLR_W(b128_random[i], b128_random[j],
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
