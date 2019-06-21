/*
 *  Test program for MSA instruction BSET.W
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
    char *instruction_name =  "BSET.W";
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
        { 0x8000000080000000ULL, 0x8000000080000000ULL, },    /*   8  */
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },
        { 0x0000040000000400ULL, 0x0000040000000400ULL, },
        { 0x0020000000200000ULL, 0x0020000000200000ULL, },
        { 0x0000100000001000ULL, 0x0000100000001000ULL, },
        { 0x0008000000080000ULL, 0x0008000000080000ULL, },
        { 0x0000000800004000ULL, 0x0100000000000008ULL, },
        { 0x1000000000020000ULL, 0x0000008010000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },    /*  16  */
        { 0xaaaaaaabaaaaaaabULL, 0xaaaaaaabaaaaaaabULL, },
        { 0xaaaaaeaaaaaaaeaaULL, 0xaaaaaeaaaaaaaeaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaabaaaaaaabaaaULL, 0xaaaabaaaaaaabaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaeaaaULL, 0xabaaaaaaaaaaaaaaULL, },
        { 0xbaaaaaaaaaaaaaaaULL, 0xaaaaaaaabaaaaaaaULL, },
        { 0xd5555555d5555555ULL, 0xd5555555d5555555ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5575555555755555ULL, 0x5575555555755555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x555d5555555d5555ULL, 0x555d5555555d5555ULL, },
        { 0x5555555d55555555ULL, 0x555555555555555dULL, },
        { 0x5555555555575555ULL, 0x555555d555555555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },    /*  32  */
        { 0xcccccccdcccccccdULL, 0xcccccccdcccccccdULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccecccccccecccccULL, 0xccecccccccecccccULL, },
        { 0xccccdcccccccdcccULL, 0xccccdcccccccdcccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xcdccccccccccccccULL, },
        { 0xdcccccccccceccccULL, 0xccccccccdcccccccULL, },
        { 0xb3333333b3333333ULL, 0xb3333333b3333333ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333373333333733ULL, 0x3333373333333733ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x333b3333333b3333ULL, 0x333b3333333b3333ULL, },
        { 0x3333333b33337333ULL, 0x333333333333333bULL, },
        { 0x3333333333333333ULL, 0x333333b333333333ULL, },
        { 0xe38e38e38e38e38eULL, 0xb8e38e38e38e38e3ULL, },    /*  48  */
        { 0xe38e38e38e38e38fULL, 0x38e38e39e38e38e3ULL, },
        { 0xe38e3ce38e38e78eULL, 0x38e38e38e38e3ce3ULL, },
        { 0xe3ae38e38e38e38eULL, 0x38e38e38e3ae38e3ULL, },
        { 0xe38e38e38e38f38eULL, 0x38e39e38e38e38e3ULL, },
        { 0xe38e38e38e38e38eULL, 0x38eb8e38e38e38e3ULL, },
        { 0xe38e38eb8e38e38eULL, 0x39e38e38e38e38ebULL, },
        { 0xf38e38e38e3ae38eULL, 0x38e38eb8f38e38e3ULL, },
        { 0x9c71c71cf1c71c71ULL, 0xc71c71c79c71c71cULL, },    /*  56  */
        { 0x1c71c71d71c71c71ULL, 0xc71c71c71c71c71dULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c75c71c71c71cULL, },
        { 0x1c71c71c71e71c71ULL, 0xc73c71c71c71c71cULL, },
        { 0x1c71d71c71c71c71ULL, 0xc71c71c71c71d71cULL, },
        { 0x1c79c71c71cf1c71ULL, 0xc71c71c71c79c71cULL, },
        { 0x1c71c71c71c75c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x886af6cc28625541ULL, 0x4b670b5efe7bb00cULL, },    /*  64  */
        { 0x886ae6cc28625540ULL, 0x4f670b5efe7bb00cULL, },
        { 0x886ae6cc28625541ULL, 0xcb670b5efe7bb00cULL, },
        { 0x886ae6cc28625540ULL, 0x4b670b5efe7bb00dULL, },
        { 0xfbbe10634d93c709ULL, 0x52f7bb1a153f52fcULL, },
        { 0xfbbe006b4d93c708ULL, 0x16f7bb1a153f52fcULL, },
        { 0xfbbe04634d93c709ULL, 0x92f7bb1a153f52fcULL, },
        { 0xfbbe20634d93c708ULL, 0x13f7bb1a153f52fdULL, },
        { 0xac5abeaab9cf8b81ULL, 0x67d8c6ffab2b3514ULL, },    /*  72  */
        { 0xac5aaeaab9cf8b80ULL, 0x27d8c6ffbb2b2514ULL, },
        { 0xac5aaeaab9cf8b81ULL, 0xa7d8c6ffab3b2514ULL, },
        { 0xac5aaeaab9cfcb80ULL, 0x27d8c6ffab2b2515ULL, },
        { 0x704f164d5e31e24fULL, 0xcdf188d8a942f2a0ULL, },
        { 0x704f164d5e31e34eULL, 0x8df188d8b942e2a0ULL, },
        { 0x704f164d5e31e24fULL, 0x8df188d8a952e2a0ULL, },
        { 0x704f364d5e31e24eULL, 0x8df188d8a942e2a1ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_BSET_W(b128_pattern[i], b128_pattern[j],
                          b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_BSET_W(b128_random[i], b128_random[j],
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
