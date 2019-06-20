/*
 *  Test program for MSA instruction ADDS_A.B
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
    char *instruction_name =  "ADDS_A.B";
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
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x737f7f737f7f737fULL, 0x7f737f7f737f7f73ULL, },
        { 0x727f7f727f7f727fULL, 0x7f727f7f727f7f72ULL, },
        { 0x5656565656565656ULL, 0x5656565656565656ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x727f7f727f7f727fULL, 0x7f727f7f727f7f72ULL, },
        { 0x717f7f717f7f717fULL, 0x7f717f7f717f7f71ULL, },
        { 0x3535353535353535ULL, 0x3535353535353535ULL, },    /*  32  */
        { 0x3434343434343434ULL, 0x3434343434343434ULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x6868686868686868ULL, 0x6868686868686868ULL, },
        { 0x6767676767676767ULL, 0x6767676767676767ULL, },
        { 0x517f6c517f6c517fULL, 0x6c517f6c517f6c51ULL, },
        { 0x507f6d507f6d507fULL, 0x6d507f6d507f6d50ULL, },
        { 0x3434343434343434ULL, 0x3434343434343434ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x6767676767676767ULL, 0x6767676767676767ULL, },
        { 0x6666666666666666ULL, 0x6666666666666666ULL, },
        { 0x507f6b507f6b507fULL, 0x6b507f6b507f6b50ULL, },
        { 0x4f7f6c4f7f6c4f7fULL, 0x6c4f7f6c4f7f6c4fULL, },
        { 0x1e73391e73391e73ULL, 0x391e73391e73391eULL, },    /*  48  */
        { 0x1d72381d72381d72ULL, 0x381d72381d72381dULL, },
        { 0x737f7f737f7f737fULL, 0x7f737f7f737f7f73ULL, },
        { 0x727f7f727f7f727fULL, 0x7f727f7f727f7f72ULL, },
        { 0x517f6c517f6c517fULL, 0x6c517f6c517f6c51ULL, },
        { 0x507f6b507f6b507fULL, 0x6b507f6b507f6b50ULL, },
        { 0x3a7f703a7f703a7fULL, 0x703a7f703a7f703aULL, },
        { 0x397f71397f71397fULL, 0x71397f71397f7139ULL, },
        { 0x1d723a1d723a1d72ULL, 0x3a1d723a1d723a1dULL, },    /*  56  */
        { 0x1c71391c71391c71ULL, 0x391c71391c71391cULL, },
        { 0x727f7f727f7f727fULL, 0x7f727f7f727f7f72ULL, },
        { 0x717f7f717f7f717fULL, 0x7f717f7f717f7f71ULL, },
        { 0x507f6d507f6d507fULL, 0x6d507f6d507f6d50ULL, },
        { 0x4f7f6c4f7f6c4f7fULL, 0x6c4f7f6c4f7f6c4fULL, },
        { 0x397f71397f71397fULL, 0x71397f71397f7139ULL, },
        { 0x387f72387f72387fULL, 0x72387f72387f7238ULL, },
        { 0x7f7f3468507f7f7fULL, 0x7f7f167f047f7f18ULL, },    /*  64  */
        { 0x7d7f1a7f757f7f48ULL, 0x5d705078177f7f10ULL, },
        { 0x7f7f6c7f6f7f7f7fULL, 0x727f455f577f7520ULL, },
        { 0x7f7f307f7f7f737fULL, 0x7f767f7f597f6e6cULL, },
        { 0x7d7f1a7f757f7f48ULL, 0x5d705078177f7f10ULL, },
        { 0x0a7f007f7f7f7210ULL, 0x24127f342a7e7f08ULL, },
        { 0x597f527f7f7f7f7fULL, 0x39317f1b6a6a7718ULL, },
        { 0x757f167f7f7f5756ULL, 0x7f187f426c7f7064ULL, },
        { 0x7f7f6c7f6f7f7f7fULL, 0x727f455f577f7520ULL, },    /*  72  */
        { 0x597f527f7f7f7f7fULL, 0x39317f1b6a6a7718ULL, },
        { 0x7f7f7f7f7f627f7fULL, 0x4e5074027f564a28ULL, },
        { 0x7f7f687f7f627f7fULL, 0x7f377f297f6d4374ULL, },
        { 0x7f7f307f7f7f737fULL, 0x7f767f7f597f6e6cULL, },
        { 0x757f167f7f7f5756ULL, 0x7f187f426c7f7064ULL, },
        { 0x7f7f687f7f627f7fULL, 0x7f377f297f6d4374ULL, },
        { 0x7f7f2c7f7f623c7fULL, 0x7f1e7f507f7f3c7fULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_ADDS_A_B(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_ADDS_A_B(b128_random[i], b128_random[j],
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
