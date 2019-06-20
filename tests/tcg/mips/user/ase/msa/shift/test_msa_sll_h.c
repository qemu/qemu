/*
 *  Test program for MSA instruction SLL.H
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
    char *group_name = "Shift";
    char *instruction_name =  "SLL.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x8000800080008000ULL, 0x8000800080008000ULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xfc00fc00fc00fc00ULL, 0xfc00fc00fc00fc00ULL, },
        { 0xffe0ffe0ffe0ffe0ULL, 0xffe0ffe0ffe0ffe0ULL, },
        { 0xf000f000f000f000ULL, 0xf000f000f000f000ULL, },
        { 0xfff8fff8fff8fff8ULL, 0xfff8fff8fff8fff8ULL, },
        { 0xc000fff8ff00c000ULL, 0xfff8ff00c000fff8ULL, },
        { 0xfffef000ff80fffeULL, 0xf000ff80fffef000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xa800a800a800a800ULL, 0xa800a800a800a800ULL, },
        { 0x5540554055405540ULL, 0x5540554055405540ULL, },
        { 0xa000a000a000a000ULL, 0xa000a000a000a000ULL, },
        { 0x5550555055505550ULL, 0x5550555055505550ULL, },
        { 0x80005550aa008000ULL, 0x5550aa0080005550ULL, },
        { 0x5554a00055005554ULL, 0xa00055005554a000ULL, },
        { 0x8000800080008000ULL, 0x8000800080008000ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5400540054005400ULL, 0x5400540054005400ULL, },
        { 0xaaa0aaa0aaa0aaa0ULL, 0xaaa0aaa0aaa0aaa0ULL, },
        { 0x5000500050005000ULL, 0x5000500050005000ULL, },
        { 0xaaa8aaa8aaa8aaa8ULL, 0xaaa8aaa8aaa8aaa8ULL, },
        { 0x4000aaa855004000ULL, 0xaaa855004000aaa8ULL, },
        { 0xaaaa5000aa80aaaaULL, 0x5000aa80aaaa5000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x3000300030003000ULL, 0x3000300030003000ULL, },
        { 0x9980998099809980ULL, 0x9980998099809980ULL, },
        { 0xc000c000c000c000ULL, 0xc000c000c000c000ULL, },
        { 0x6660666066606660ULL, 0x6660666066606660ULL, },
        { 0x00006660cc000000ULL, 0x6660cc0000006660ULL, },
        { 0x9998c00066009998ULL, 0xc00066009998c000ULL, },
        { 0x8000800080008000ULL, 0x8000800080008000ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xcc00cc00cc00cc00ULL, 0xcc00cc00cc00cc00ULL, },
        { 0x6660666066606660ULL, 0x6660666066606660ULL, },
        { 0x3000300030003000ULL, 0x3000300030003000ULL, },
        { 0x9998999899989998ULL, 0x9998999899989998ULL, },
        { 0xc00099983300c000ULL, 0x99983300c0009998ULL, },
        { 0x6666300099806666ULL, 0x3000998066663000ULL, },
        { 0x0000800000000000ULL, 0x8000000000008000ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x38008c00e0003800ULL, 0x8c00e00038008c00ULL, },
        { 0x71c01c60c70071c0ULL, 0x1c60c70071c01c60ULL, },
        { 0xe00030008000e000ULL, 0x30008000e0003000ULL, },
        { 0x1c70c71871c01c70ULL, 0xc71871c01c70c718ULL, },
        { 0x8000c71838008000ULL, 0xc71838008000c718ULL, },
        { 0xc71c30001c00c71cULL, 0x30001c00c71c3000ULL, },
        { 0x8000000080008000ULL, 0x0000800080000000ULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0xc40070001c00c400ULL, 0x70001c00c4007000ULL, },
        { 0x8e20e38038e08e20ULL, 0xe38038e08e20e380ULL, },
        { 0x1000c00070001000ULL, 0xc00070001000c000ULL, },
        { 0xe38838e08e38e388ULL, 0x38e08e38e38838e0ULL, },
        { 0x400038e0c7004000ULL, 0x38e0c700400038e0ULL, },
        { 0x38e2c000e38038e2ULL, 0xc000e38038e2c000ULL, },
        { 0xa800c000a1885540ULL, 0xb3808000d800c000ULL, },    /*  64  */
        { 0x8000366043104000ULL, 0xb38078008000c000ULL, },
        { 0xa800300000005540ULL, 0x67000000d80000c0ULL, },
        { 0x0000800050c40000ULL, 0x96ce5e00f9ecb00cULL, },
        { 0xf8003000364cc708ULL, 0x7b808000f800c000ULL, },
        { 0x800003186c980800ULL, 0x7b8068008000c000ULL, },
        { 0xf8008c008000c708ULL, 0xf7000000f8002fc0ULL, },
        { 0x000060009b260000ULL, 0x25ee1a0054fc52fcULL, },
        { 0x6800a000e73c8b80ULL, 0xec00c00058004000ULL, },    /*  72  */
        { 0x80007550ce788000ULL, 0xec00fc0080004000ULL, },
        { 0x6800a80080008b80ULL, 0xd800800058005140ULL, },
        { 0x00004000739e0000ULL, 0x4fb0ff00acac2514ULL, },
        { 0x3c00d00078c4e24eULL, 0xf880000010000000ULL, },
        { 0xc000b268f1884e00ULL, 0xf880600000000000ULL, },
        { 0x3c0034008000e24eULL, 0xf100000010002a00ULL, },
        { 0x8000a000bc628000ULL, 0x1be2d800a508e2a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_SLL_H(b128_pattern[i], b128_pattern[j],
                         b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_SLL_H(b128_random[i], b128_random[j],
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
