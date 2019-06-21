/*
 *  Test program for MSA instruction ADD_A.B
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
    char *group_name = "Int Add";
    char *instruction_name =  "ADD_A.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0202020202020202ULL, 0x0202020202020202ULL, },    /*   0  */
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0x5757575757575757ULL, 0x5757575757575757ULL, },
        { 0x5656565656565656ULL, 0x5656565656565656ULL, },
        { 0x3535353535353535ULL, 0x3535353535353535ULL, },
        { 0x3434343434343434ULL, 0x3434343434343434ULL, },
        { 0x1e73391e73391e73ULL, 0x391e73391e73391eULL, },
        { 0x1d723a1d723a1d72ULL, 0x3a1d723a1d723a1dULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5656565656565656ULL, 0x5656565656565656ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x3434343434343434ULL, 0x3434343434343434ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x1d72381d72381d72ULL, 0x381d72381d72381dULL, },
        { 0x1c71391c71391c71ULL, 0x391c71391c71391cULL, },
        { 0x5757575757575757ULL, 0x5757575757575757ULL, },    /*  16  */
        { 0x5656565656565656ULL, 0x5656565656565656ULL, },
        { 0xacacacacacacacacULL, 0xacacacacacacacacULL, },
        { 0xababababababababULL, 0xababababababababULL, },
        { 0x8a8a8a8a8a8a8a8aULL, 0x8a8a8a8a8a8a8a8aULL, },
        { 0x8989898989898989ULL, 0x8989898989898989ULL, },
        { 0x73c88e73c88e73c8ULL, 0x8e73c88e73c88e73ULL, },
        { 0x72c78f72c78f72c7ULL, 0x8f72c78f72c78f72ULL, },
        { 0x5656565656565656ULL, 0x5656565656565656ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xababababababababULL, 0xababababababababULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x8989898989898989ULL, 0x8989898989898989ULL, },
        { 0x8888888888888888ULL, 0x8888888888888888ULL, },
        { 0x72c78d72c78d72c7ULL, 0x8d72c78d72c78d72ULL, },
        { 0x71c68e71c68e71c6ULL, 0x8e71c68e71c68e71ULL, },
        { 0x3535353535353535ULL, 0x3535353535353535ULL, },    /*  32  */
        { 0x3434343434343434ULL, 0x3434343434343434ULL, },
        { 0x8a8a8a8a8a8a8a8aULL, 0x8a8a8a8a8a8a8a8aULL, },
        { 0x8989898989898989ULL, 0x8989898989898989ULL, },
        { 0x6868686868686868ULL, 0x6868686868686868ULL, },
        { 0x6767676767676767ULL, 0x6767676767676767ULL, },
        { 0x51a66c51a66c51a6ULL, 0x6c51a66c51a66c51ULL, },
        { 0x50a56d50a56d50a5ULL, 0x6d50a56d50a56d50ULL, },
        { 0x3434343434343434ULL, 0x3434343434343434ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x8989898989898989ULL, 0x8989898989898989ULL, },
        { 0x8888888888888888ULL, 0x8888888888888888ULL, },
        { 0x6767676767676767ULL, 0x6767676767676767ULL, },
        { 0x6666666666666666ULL, 0x6666666666666666ULL, },
        { 0x50a56b50a56b50a5ULL, 0x6b50a56b50a56b50ULL, },
        { 0x4fa46c4fa46c4fa4ULL, 0x6c4fa46c4fa46c4fULL, },
        { 0x1e73391e73391e73ULL, 0x391e73391e73391eULL, },    /*  48  */
        { 0x1d72381d72381d72ULL, 0x381d72381d72381dULL, },
        { 0x73c88e73c88e73c8ULL, 0x8e73c88e73c88e73ULL, },
        { 0x72c78d72c78d72c7ULL, 0x8d72c78d72c78d72ULL, },
        { 0x51a66c51a66c51a6ULL, 0x6c51a66c51a66c51ULL, },
        { 0x50a56b50a56b50a5ULL, 0x6b50a56b50a56b50ULL, },
        { 0x3ae4703ae4703ae4ULL, 0x703ae4703ae4703aULL, },
        { 0x39e37139e37139e3ULL, 0x7139e37139e37139ULL, },
        { 0x1d723a1d723a1d72ULL, 0x3a1d723a1d723a1dULL, },    /*  56  */
        { 0x1c71391c71391c71ULL, 0x391c71391c71391cULL, },
        { 0x72c78f72c78f72c7ULL, 0x8f72c78f72c78f72ULL, },
        { 0x71c68e71c68e71c6ULL, 0x8e71c68e71c68e71ULL, },
        { 0x50a56d50a56d50a5ULL, 0x6d50a56d50a56d50ULL, },
        { 0x4fa46c4fa46c4fa4ULL, 0x6c4fa46c4fa46c4fULL, },
        { 0x39e37139e37139e3ULL, 0x7139e37139e37139ULL, },
        { 0x38e27238e27238e2ULL, 0x7238e27238e27238ULL, },
        { 0xf0d4346850c4aa80ULL, 0x96ce16bc04f6a018ULL, },    /*  64  */
        { 0x7dac1a9775cf8e48ULL, 0x5d70507817baa210ULL, },
        { 0xccc46c8a6f93cac0ULL, 0x728f455f57a67520ULL, },
        { 0xe8b930818693738eULL, 0xbe76838659bd6e6cULL, },
        { 0x7dac1a9775cf8e48ULL, 0x5d70507817baa210ULL, },
        { 0x0a8400c69ada7210ULL, 0x24128a342a7ea408ULL, },
        { 0x599c52b9949eae88ULL, 0x39317f1b6a6a7718ULL, },
        { 0x759116b0ab9e5756ULL, 0x8518bd426c817064ULL, },
        { 0xccc46c8a6f93cac0ULL, 0x728f455f57a67520ULL, },    /*  72  */
        { 0x599c52b9949eae88ULL, 0x39317f1b6a6a7718ULL, },
        { 0xa8b4a4ac8e62ea00ULL, 0x4e507402aa564a28ULL, },
        { 0xc4a968a3a56293ceULL, 0x9a37b229ac6d4374ULL, },
        { 0xe8b930818693738eULL, 0xbe76838659bd6e6cULL, },
        { 0x759116b0ab9e5756ULL, 0x8518bd426c817064ULL, },
        { 0xc4a968a3a56293ceULL, 0x9a37b229ac6d4374ULL, },
        { 0xe09e2c9abc623c9cULL, 0xe61ef050ae843cc0ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_ADD_A_B(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_ADD_A_B(b128_random[i], b128_random[j],
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
