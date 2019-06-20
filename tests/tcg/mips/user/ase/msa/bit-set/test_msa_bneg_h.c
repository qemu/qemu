/*
 *  Test program for MSA instruction BNEG.H
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
    char *instruction_name =  "BNEG.H";
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
        { 0x8000800080008000ULL, 0x8000800080008000ULL, },    /*   8  */
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x0400040004000400ULL, 0x0400040004000400ULL, },
        { 0x0020002000200020ULL, 0x0020002000200020ULL, },
        { 0x1000100010001000ULL, 0x1000100010001000ULL, },
        { 0x0008000800080008ULL, 0x0008000800080008ULL, },
        { 0x4000000801004000ULL, 0x0008010040000008ULL, },
        { 0x0002100000800002ULL, 0x1000008000021000ULL, },
        { 0x2aaa2aaa2aaa2aaaULL, 0x2aaa2aaa2aaa2aaaULL, },    /*  16  */
        { 0xaaabaaabaaabaaabULL, 0xaaabaaabaaabaaabULL, },
        { 0xaeaaaeaaaeaaaeaaULL, 0xaeaaaeaaaeaaaeaaULL, },
        { 0xaa8aaa8aaa8aaa8aULL, 0xaa8aaa8aaa8aaa8aULL, },
        { 0xbaaabaaabaaabaaaULL, 0xbaaabaaabaaabaaaULL, },
        { 0xaaa2aaa2aaa2aaa2ULL, 0xaaa2aaa2aaa2aaa2ULL, },
        { 0xeaaaaaa2abaaeaaaULL, 0xaaa2abaaeaaaaaa2ULL, },
        { 0xaaa8baaaaa2aaaa8ULL, 0xbaaaaa2aaaa8baaaULL, },
        { 0xd555d555d555d555ULL, 0xd555d555d555d555ULL, },    /*  24  */
        { 0x5554555455545554ULL, 0x5554555455545554ULL, },
        { 0x5155515551555155ULL, 0x5155515551555155ULL, },
        { 0x5575557555755575ULL, 0x5575557555755575ULL, },
        { 0x4555455545554555ULL, 0x4555455545554555ULL, },
        { 0x555d555d555d555dULL, 0x555d555d555d555dULL, },
        { 0x1555555d54551555ULL, 0x555d54551555555dULL, },
        { 0x5557455555d55557ULL, 0x455555d555574555ULL, },
        { 0x4ccc4ccc4ccc4cccULL, 0x4ccc4ccc4ccc4cccULL, },    /*  32  */
        { 0xcccdcccdcccdcccdULL, 0xcccdcccdcccdcccdULL, },
        { 0xc8ccc8ccc8ccc8ccULL, 0xc8ccc8ccc8ccc8ccULL, },
        { 0xccecccecccecccecULL, 0xccecccecccecccecULL, },
        { 0xdcccdcccdcccdcccULL, 0xdcccdcccdcccdcccULL, },
        { 0xccc4ccc4ccc4ccc4ULL, 0xccc4ccc4ccc4ccc4ULL, },
        { 0x8cccccc4cdcc8cccULL, 0xccc4cdcc8cccccc4ULL, },
        { 0xcccedccccc4cccceULL, 0xdccccc4ccccedcccULL, },
        { 0xb333b333b333b333ULL, 0xb333b333b333b333ULL, },    /*  40  */
        { 0x3332333233323332ULL, 0x3332333233323332ULL, },
        { 0x3733373337333733ULL, 0x3733373337333733ULL, },
        { 0x3313331333133313ULL, 0x3313331333133313ULL, },
        { 0x2333233323332333ULL, 0x2333233323332333ULL, },
        { 0x333b333b333b333bULL, 0x333b333b333b333bULL, },
        { 0x7333333b32337333ULL, 0x333b32337333333bULL, },
        { 0x3331233333b33331ULL, 0x233333b333312333ULL, },
        { 0x638eb8e30e38638eULL, 0xb8e30e38638eb8e3ULL, },    /*  48  */
        { 0xe38f38e28e39e38fULL, 0x38e28e39e38f38e2ULL, },
        { 0xe78e3ce38a38e78eULL, 0x3ce38a38e78e3ce3ULL, },
        { 0xe3ae38c38e18e3aeULL, 0x38c38e18e3ae38c3ULL, },
        { 0xf38e28e39e38f38eULL, 0x28e39e38f38e28e3ULL, },
        { 0xe38638eb8e30e386ULL, 0x38eb8e30e38638ebULL, },
        { 0xa38e38eb8f38a38eULL, 0x38eb8f38a38e38ebULL, },
        { 0xe38c28e38eb8e38cULL, 0x28e38eb8e38c28e3ULL, },
        { 0x9c71471cf1c79c71ULL, 0x471cf1c79c71471cULL, },    /*  56  */
        { 0x1c70c71d71c61c70ULL, 0xc71d71c61c70c71dULL, },
        { 0x1871c31c75c71871ULL, 0xc31c75c71871c31cULL, },
        { 0x1c51c73c71e71c51ULL, 0xc73c71e71c51c73cULL, },
        { 0x0c71d71c61c70c71ULL, 0xd71c61c70c71d71cULL, },
        { 0x1c79c71471cf1c79ULL, 0xc71471cf1c79c714ULL, },
        { 0x5c71c71470c75c71ULL, 0xc71470c75c71c714ULL, },
        { 0x1c73d71c71471c73ULL, 0xd71c71471c73d71cULL, },
        { 0x8c6af6cc28665541ULL, 0x4be74b5ef67ba00cULL, },    /*  64  */
        { 0xc86ae6c4286a5440ULL, 0x4be70f5e7e7ba00cULL, },
        { 0x8c6ae2cca8625541ULL, 0x4a678b5ef67bb01cULL, },
        { 0x086ac6cc28601540ULL, 0x4b650a5efe7fb00dULL, },
        { 0xffbe10634d97c709ULL, 0x1277fb1a1d3f42fcULL, },
        { 0xbbbe006b4d9bc608ULL, 0x1277bf1a953f42fcULL, },
        { 0xffbe0463cd93c709ULL, 0x13f73b1a1d3f52ecULL, },
        { 0x7bbe20634d918708ULL, 0x12f5ba1a153b52fdULL, },
        { 0xa85abeaab9cb8b81ULL, 0x275886ffa32b3514ULL, },    /*  72  */
        { 0xec5aaea2b9c78a80ULL, 0x2758c2ff2b2b3514ULL, },
        { 0xa85aaaaa39cf8b81ULL, 0x26d846ffa32b2504ULL, },
        { 0x2c5a8eaab9cdcb80ULL, 0x27dac7ffab2f2515ULL, },
        { 0x744f064d5e35e24fULL, 0x8d71c8d8a142f2a0ULL, },
        { 0x304f16455e39e34eULL, 0x8d718cd82942f2a0ULL, },
        { 0x744f124dde31e24fULL, 0x8cf108d8a142e2b0ULL, },
        { 0xf04f364d5e33a24eULL, 0x8df389d8a946e2a1ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_BNEG_H(b128_pattern[i], b128_pattern[j],
                          b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_BNEG_H(b128_random[i], b128_random[j],
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
