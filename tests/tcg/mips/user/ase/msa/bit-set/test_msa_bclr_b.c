/*
 *  Test program for MSA instruction BCLR.B
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
    char *instruction_name =  "BCLR.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },    /*   0  */
        { 0xfefefefefefefefeULL, 0xfefefefefefefefeULL, },
        { 0xfbfbfbfbfbfbfbfbULL, 0xfbfbfbfbfbfbfbfbULL, },
        { 0xdfdfdfdfdfdfdfdfULL, 0xdfdfdfdfdfdfdfdfULL, },
        { 0xefefefefefefefefULL, 0xefefefefefefefefULL, },
        { 0xf7f7f7f7f7f7f7f7ULL, 0xf7f7f7f7f7f7f7f7ULL, },
        { 0xf7bffef7bffef7bfULL, 0xfef7bffef7bffef7ULL, },
        { 0xeffd7feffd7feffdULL, 0x7feffd7feffd7fefULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x2a2a2a2a2a2a2a2aULL, 0x2a2a2a2a2a2a2a2aULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x8a8a8a8a8a8a8a8aULL, 0x8a8a8a8a8a8a8a8aULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xa2a2a2a2a2a2a2a2ULL, 0xa2a2a2a2a2a2a2a2ULL, },
        { 0xa2aaaaa2aaaaa2aaULL, 0xaaa2aaaaa2aaaaa2ULL, },
        { 0xaaa82aaaa82aaaa8ULL, 0x2aaaa82aaaa82aaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },    /*  24  */
        { 0x5454545454545454ULL, 0x5454545454545454ULL, },
        { 0x5151515151515151ULL, 0x5151515151515151ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x4545454545454545ULL, 0x4545454545454545ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5515545515545515ULL, 0x5455155455155455ULL, },
        { 0x4555554555554555ULL, 0x5545555545555545ULL, },
        { 0x4c4c4c4c4c4c4c4cULL, 0x4c4c4c4c4c4c4c4cULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xc8c8c8c8c8c8c8c8ULL, 0xc8c8c8c8c8c8c8c8ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xc4c4c4c4c4c4c4c4ULL, 0xc4c4c4c4c4c4c4c4ULL, },
        { 0xc48cccc48cccc48cULL, 0xccc48cccc48cccc4ULL, },
        { 0xcccc4ccccc4cccccULL, 0x4ccccc4ccccc4cccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },    /*  40  */
        { 0x3232323232323232ULL, 0x3232323232323232ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x1313131313131313ULL, 0x1313131313131313ULL, },
        { 0x2323232323232323ULL, 0x2323232323232323ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333323333323333ULL, 0x3233333233333233ULL, },
        { 0x2331332331332331ULL, 0x3323313323313323ULL, },
        { 0x630e38630e38630eULL, 0x38630e38630e3863ULL, },    /*  48  */
        { 0xe28e38e28e38e28eULL, 0x38e28e38e28e38e2ULL, },
        { 0xe38a38e38a38e38aULL, 0x38e38a38e38a38e3ULL, },
        { 0xc38e18c38e18c38eULL, 0x18c38e18c38e18c3ULL, },
        { 0xe38e28e38e28e38eULL, 0x28e38e28e38e28e3ULL, },
        { 0xe38630e38630e386ULL, 0x30e38630e38630e3ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xe38c38e38c38e38cULL, 0x38e38c38e38c38e3ULL, },
        { 0x1c71471c71471c71ULL, 0x471c71471c71471cULL, },    /*  56  */
        { 0x1c70c61c70c61c70ULL, 0xc61c70c61c70c61cULL, },
        { 0x1871c31871c31871ULL, 0xc31871c31871c318ULL, },
        { 0x1c51c71c51c71c51ULL, 0xc71c51c71c51c71cULL, },
        { 0x0c61c70c61c70c61ULL, 0xc70c61c70c61c70cULL, },
        { 0x1471c71471c71471ULL, 0xc71471c71471c714ULL, },
        { 0x1431c61431c61431ULL, 0xc61431c61431c614ULL, },
        { 0x0c71470c71470c71ULL, 0x470c71470c71470cULL, },
        { 0x886aa6cc28625540ULL, 0x4367031ebe73b00cULL, },    /*  64  */
        { 0x802ae6c408625540ULL, 0x4b67035ade7bb00cULL, },
        { 0x886aa6c828625540ULL, 0x4b660b5ef673900cULL, },
        { 0x886aa6cc28605100ULL, 0x4b650a5efc7bb00cULL, },
        { 0xfaba00634c93c708ULL, 0x1277b31a153752ecULL, },
        { 0xf3be00634d934708ULL, 0x1277b31a153f52ecULL, },
        { 0xebba00634d13c708ULL, 0x12f6bb1a153752ecULL, },
        { 0xfa3e00430d91c308ULL, 0x12f5ba1a153b52fcULL, },
        { 0xac5aaeaab8cb8b80ULL, 0x2758c6bfab232404ULL, },    /*  72  */
        { 0xa41aaea299c70b80ULL, 0x2358c6fb8b2b2104ULL, },
        { 0xac5aaeaab94f8380ULL, 0x27d8867fa3230504ULL, },
        { 0xac5aae8ab9cd8b80ULL, 0x07d8c6fea92b2114ULL, },
        { 0x704b164d5e31c24eULL, 0x85718098a942e2a0ULL, },
        { 0x700f16455e31624eULL, 0x897180d88942e2a0ULL, },
        { 0x604b16495c31e24eULL, 0x0df08858a142c2a0ULL, },
        { 0x704f164d1e31e20eULL, 0x8df188d8a942e2a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_BCLR_B(b128_pattern[i], b128_pattern[j],
                          b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_BCLR_B(b128_random[i], b128_random[j],
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
