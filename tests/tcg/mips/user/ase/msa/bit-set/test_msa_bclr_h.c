/*
 *  Test program for MSA instruction BCLR.H
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
    char *instruction_name =  "BCLR.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x7fff7fff7fff7fffULL, 0x7fff7fff7fff7fffULL, },    /*   0  */
        { 0xfffefffefffefffeULL, 0xfffefffefffefffeULL, },
        { 0xfbfffbfffbfffbffULL, 0xfbfffbfffbfffbffULL, },
        { 0xffdfffdfffdfffdfULL, 0xffdfffdfffdfffdfULL, },
        { 0xefffefffefffefffULL, 0xefffefffefffefffULL, },
        { 0xfff7fff7fff7fff7ULL, 0xfff7fff7fff7fff7ULL, },
        { 0xbffffff7feffbfffULL, 0xfff7feffbffffff7ULL, },
        { 0xfffdefffff7ffffdULL, 0xefffff7ffffdefffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x2aaa2aaa2aaa2aaaULL, 0x2aaa2aaa2aaa2aaaULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaa8aaa8aaa8aaa8aULL, 0xaa8aaa8aaa8aaa8aULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaa2aaa2aaa2aaa2ULL, 0xaaa2aaa2aaa2aaa2ULL, },
        { 0xaaaaaaa2aaaaaaaaULL, 0xaaa2aaaaaaaaaaa2ULL, },
        { 0xaaa8aaaaaa2aaaa8ULL, 0xaaaaaa2aaaa8aaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },    /*  24  */
        { 0x5554555455545554ULL, 0x5554555455545554ULL, },
        { 0x5155515551555155ULL, 0x5155515551555155ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x4555455545554555ULL, 0x4555455545554555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x1555555554551555ULL, 0x5555545515555555ULL, },
        { 0x5555455555555555ULL, 0x4555555555554555ULL, },
        { 0x4ccc4ccc4ccc4cccULL, 0x4ccc4ccc4ccc4cccULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xc8ccc8ccc8ccc8ccULL, 0xc8ccc8ccc8ccc8ccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccc4ccc4ccc4ccc4ULL, 0xccc4ccc4ccc4ccc4ULL, },
        { 0x8cccccc4cccc8cccULL, 0xccc4cccc8cccccc4ULL, },
        { 0xcccccccccc4cccccULL, 0xcccccc4cccccccccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },    /*  40  */
        { 0x3332333233323332ULL, 0x3332333233323332ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3313331333133313ULL, 0x3313331333133313ULL, },
        { 0x2333233323332333ULL, 0x2333233323332333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333332333333ULL, 0x3333323333333333ULL, },
        { 0x3331233333333331ULL, 0x2333333333312333ULL, },
        { 0x638e38e30e38638eULL, 0x38e30e38638e38e3ULL, },    /*  48  */
        { 0xe38e38e28e38e38eULL, 0x38e28e38e38e38e2ULL, },
        { 0xe38e38e38a38e38eULL, 0x38e38a38e38e38e3ULL, },
        { 0xe38e38c38e18e38eULL, 0x38c38e18e38e38c3ULL, },
        { 0xe38e28e38e38e38eULL, 0x28e38e38e38e28e3ULL, },
        { 0xe38638e38e30e386ULL, 0x38e38e30e38638e3ULL, },
        { 0xa38e38e38e38a38eULL, 0x38e38e38a38e38e3ULL, },
        { 0xe38c28e38e38e38cULL, 0x28e38e38e38c28e3ULL, },
        { 0x1c71471c71c71c71ULL, 0x471c71c71c71471cULL, },    /*  56  */
        { 0x1c70c71c71c61c70ULL, 0xc71c71c61c70c71cULL, },
        { 0x1871c31c71c71871ULL, 0xc31c71c71871c31cULL, },
        { 0x1c51c71c71c71c51ULL, 0xc71c71c71c51c71cULL, },
        { 0x0c71c71c61c70c71ULL, 0xc71c61c70c71c71cULL, },
        { 0x1c71c71471c71c71ULL, 0xc71471c71c71c714ULL, },
        { 0x1c71c71470c71c71ULL, 0xc71470c71c71c714ULL, },
        { 0x1c71c71c71471c71ULL, 0xc71c71471c71c71cULL, },
        { 0x886ae6cc28625540ULL, 0x4b670b5ef67ba00cULL, },    /*  64  */
        { 0x886ae6c428625440ULL, 0x4b670b5e7e7ba00cULL, },
        { 0x886ae2cc28625540ULL, 0x4a670b5ef67bb00cULL, },
        { 0x086ac6cc28601540ULL, 0x4b650a5efe7bb00cULL, },
        { 0xfbbe00634d93c708ULL, 0x1277bb1a153f42fcULL, },
        { 0xbbbe00634d93c608ULL, 0x1277bb1a153f42fcULL, },
        { 0xfbbe00634d93c708ULL, 0x12f73b1a153f52ecULL, },
        { 0x7bbe00634d918708ULL, 0x12f5ba1a153b52fcULL, },
        { 0xa85aaeaab9cb8b80ULL, 0x275886ffa32b2514ULL, },    /*  72  */
        { 0xac5aaea2b9c78a80ULL, 0x2758c2ff2b2b2514ULL, },
        { 0xa85aaaaa39cf8b80ULL, 0x26d846ffa32b2504ULL, },
        { 0x2c5a8eaab9cd8b80ULL, 0x27d8c6ffab2b2514ULL, },
        { 0x704f064d5e31e24eULL, 0x8d7188d8a142e2a0ULL, },
        { 0x304f16455e31e24eULL, 0x8d7188d82942e2a0ULL, },
        { 0x704f124d5e31e24eULL, 0x8cf108d8a142e2a0ULL, },
        { 0x704f164d5e31a24eULL, 0x8df188d8a942e2a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_BCLR_H(b128_pattern[i], b128_pattern[j],
                          b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_BCLR_H(b128_random[i], b128_random[j],
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
