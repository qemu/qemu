/*
 *  Test program for MSA instruction BSET.H
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
    char *instruction_name =  "BSET.H";
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
        { 0x8000800080008000ULL, 0x8000800080008000ULL, },    /*   8  */
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x0400040004000400ULL, 0x0400040004000400ULL, },
        { 0x0020002000200020ULL, 0x0020002000200020ULL, },
        { 0x1000100010001000ULL, 0x1000100010001000ULL, },
        { 0x0008000800080008ULL, 0x0008000800080008ULL, },
        { 0x4000000801004000ULL, 0x0008010040000008ULL, },
        { 0x0002100000800002ULL, 0x1000008000021000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },    /*  16  */
        { 0xaaabaaabaaabaaabULL, 0xaaabaaabaaabaaabULL, },
        { 0xaeaaaeaaaeaaaeaaULL, 0xaeaaaeaaaeaaaeaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xbaaabaaabaaabaaaULL, 0xbaaabaaabaaabaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xeaaaaaaaabaaeaaaULL, 0xaaaaabaaeaaaaaaaULL, },
        { 0xaaaabaaaaaaaaaaaULL, 0xbaaaaaaaaaaabaaaULL, },
        { 0xd555d555d555d555ULL, 0xd555d555d555d555ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5575557555755575ULL, 0x5575557555755575ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x555d555d555d555dULL, 0x555d555d555d555dULL, },
        { 0x5555555d55555555ULL, 0x555d55555555555dULL, },
        { 0x5557555555d55557ULL, 0x555555d555575555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },    /*  32  */
        { 0xcccdcccdcccdcccdULL, 0xcccdcccdcccdcccdULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccecccecccecccecULL, 0xccecccecccecccecULL, },
        { 0xdcccdcccdcccdcccULL, 0xdcccdcccdcccdcccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xcccccccccdccccccULL, 0xcccccdccccccccccULL, },
        { 0xcccedcccccccccceULL, 0xdccccccccccedcccULL, },
        { 0xb333b333b333b333ULL, 0xb333b333b333b333ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3733373337333733ULL, 0x3733373337333733ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x333b333b333b333bULL, 0x333b333b333b333bULL, },
        { 0x7333333b33337333ULL, 0x333b33337333333bULL, },
        { 0x3333333333b33333ULL, 0x333333b333333333ULL, },
        { 0xe38eb8e38e38e38eULL, 0xb8e38e38e38eb8e3ULL, },    /*  48  */
        { 0xe38f38e38e39e38fULL, 0x38e38e39e38f38e3ULL, },
        { 0xe78e3ce38e38e78eULL, 0x3ce38e38e78e3ce3ULL, },
        { 0xe3ae38e38e38e3aeULL, 0x38e38e38e3ae38e3ULL, },
        { 0xf38e38e39e38f38eULL, 0x38e39e38f38e38e3ULL, },
        { 0xe38e38eb8e38e38eULL, 0x38eb8e38e38e38ebULL, },
        { 0xe38e38eb8f38e38eULL, 0x38eb8f38e38e38ebULL, },
        { 0xe38e38e38eb8e38eULL, 0x38e38eb8e38e38e3ULL, },
        { 0x9c71c71cf1c79c71ULL, 0xc71cf1c79c71c71cULL, },    /*  56  */
        { 0x1c71c71d71c71c71ULL, 0xc71d71c71c71c71dULL, },
        { 0x1c71c71c75c71c71ULL, 0xc71c75c71c71c71cULL, },
        { 0x1c71c73c71e71c71ULL, 0xc73c71e71c71c73cULL, },
        { 0x1c71d71c71c71c71ULL, 0xd71c71c71c71d71cULL, },
        { 0x1c79c71c71cf1c79ULL, 0xc71c71cf1c79c71cULL, },
        { 0x5c71c71c71c75c71ULL, 0xc71c71c75c71c71cULL, },
        { 0x1c73d71c71c71c73ULL, 0xd71c71c71c73d71cULL, },
        { 0x8c6af6cc28665541ULL, 0x4be74b5efe7bb00cULL, },    /*  64  */
        { 0xc86ae6cc286a5540ULL, 0x4be70f5efe7bb00cULL, },
        { 0x8c6ae6cca8625541ULL, 0x4b678b5efe7bb01cULL, },
        { 0x886ae6cc28625540ULL, 0x4b670b5efe7fb00dULL, },
        { 0xffbe10634d97c709ULL, 0x12f7fb1a1d3f52fcULL, },
        { 0xfbbe006b4d9bc708ULL, 0x12f7bf1a953f52fcULL, },
        { 0xffbe0463cd93c709ULL, 0x13f7bb1a1d3f52fcULL, },
        { 0xfbbe20634d93c708ULL, 0x12f7bb1a153f52fdULL, },
        { 0xac5abeaab9cf8b81ULL, 0x27d8c6ffab2b3514ULL, },    /*  72  */
        { 0xec5aaeaab9cf8b80ULL, 0x27d8c6ffab2b3514ULL, },
        { 0xac5aaeaab9cf8b81ULL, 0x27d8c6ffab2b2514ULL, },
        { 0xac5aaeaab9cfcb80ULL, 0x27dac7ffab2f2515ULL, },
        { 0x744f164d5e35e24fULL, 0x8df1c8d8a942f2a0ULL, },
        { 0x704f164d5e39e34eULL, 0x8df18cd8a942f2a0ULL, },
        { 0x744f164dde31e24fULL, 0x8df188d8a942e2b0ULL, },
        { 0xf04f364d5e33e24eULL, 0x8df389d8a946e2a1ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_BSET_H(b128_pattern[i], b128_pattern[j],
                          b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_BSET_H(b128_random[i], b128_random[j],
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
