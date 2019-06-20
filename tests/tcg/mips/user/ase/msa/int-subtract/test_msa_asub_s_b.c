/*
 *  Test program for MSA instruction ASUB_S.B
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
    char *group_name = "Int Subtract";
    char *instruction_name =  "ASUB_S.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5656565656565656ULL, 0x5656565656565656ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3434343434343434ULL, 0x3434343434343434ULL, },
        { 0x1c71391c71391c71ULL, 0x391c71391c71391cULL, },
        { 0x1d72381d72381d72ULL, 0x381d72381d72381dULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5656565656565656ULL, 0x5656565656565656ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x3434343434343434ULL, 0x3434343434343434ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x1d72381d72381d72ULL, 0x381d72381d72381dULL, },
        { 0x1c71391c71391c71ULL, 0x391c71391c71391cULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },    /*  16  */
        { 0x5656565656565656ULL, 0x5656565656565656ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xababababababababULL, 0xababababababababULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x8989898989898989ULL, 0x8989898989898989ULL, },
        { 0x391c8e391c8e391cULL, 0x8e391c8e391c8e39ULL, },
        { 0x72c71d72c71d72c7ULL, 0x1d72c71d72c71d72ULL, },
        { 0x5656565656565656ULL, 0x5656565656565656ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xababababababababULL, 0xababababababababULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x8989898989898989ULL, 0x8989898989898989ULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x72c71d72c71d72c7ULL, 0x1d72c71d72c71d72ULL, },
        { 0x391c8e391c8e391cULL, 0x8e391c8e391c8e39ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },    /*  32  */
        { 0x3434343434343434ULL, 0x3434343434343434ULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x8989898989898989ULL, 0x8989898989898989ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x6767676767676767ULL, 0x6767676767676767ULL, },
        { 0x173e6c173e6c173eULL, 0x6c173e6c173e6c17ULL, },
        { 0x50a50550a50550a5ULL, 0x0550a50550a50550ULL, },
        { 0x3434343434343434ULL, 0x3434343434343434ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x8989898989898989ULL, 0x8989898989898989ULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x6767676767676767ULL, 0x6767676767676767ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x50a50550a50550a5ULL, 0x0550a50550a50550ULL, },
        { 0x173e6c173e6c173eULL, 0x6c173e6c173e6c17ULL, },
        { 0x1c71391c71391c71ULL, 0x391c71391c71391cULL, },    /*  48  */
        { 0x1d72381d72381d72ULL, 0x381d72381d72381dULL, },
        { 0x391c8e391c8e391cULL, 0x8e391c8e391c8e39ULL, },
        { 0x72c71d72c71d72c7ULL, 0x1d72c71d72c71d72ULL, },
        { 0x173e6c173e6c173eULL, 0x6c173e6c173e6c17ULL, },
        { 0x50a50550a50550a5ULL, 0x0550a50550a50550ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x39e37139e37139e3ULL, 0x7139e37139e37139ULL, },
        { 0x1d72381d72381d72ULL, 0x381d72381d72381dULL, },    /*  56  */
        { 0x1c71391c71391c71ULL, 0x391c71391c71391cULL, },
        { 0x72c71d72c71d72c7ULL, 0x1d72c71d72c71d72ULL, },
        { 0x391c8e391c8e391cULL, 0x8e391c8e391c8e39ULL, },
        { 0x50a50550a50550a5ULL, 0x0550a50550a50550ULL, },
        { 0x173e6c173e6c173eULL, 0x6c173e6c173e6c17ULL, },
        { 0x39e37139e37139e3ULL, 0x7139e37139e37139ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  64  */
        { 0x73ac1a9725cf8e38ULL, 0x39705044173ca210ULL, },
        { 0x241038226f93cac0ULL, 0x248f455f53507508ULL, },
        { 0xe81b30813631730eULL, 0xbe7683865539326cULL, },
        { 0x73ac1a9725cf8e38ULL, 0x39705044173ca210ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x4f9c52b9943c3c88ULL, 0x151f0b1b6a142d18ULL, },
        { 0x75911616119e1b46ULL, 0x850633426c03705cULL, },
        { 0x241038226f93cac0ULL, 0x248f455f53507508ULL, },    /*  72  */
        { 0x4f9c52b9943c3c88ULL, 0x151f0b1b6a142d18ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xc40b68a3a56257ceULL, 0x9a193e2702174374ULL, },
        { 0xe81b30813631730eULL, 0xbe7683865539326cULL, },
        { 0x75911616119e1b46ULL, 0x850633426c03705cULL, },
        { 0xc40b68a3a56257ceULL, 0x9a193e2702174374ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_ASUB_S_B(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_ASUB_S_B(b128_random[i], b128_random[j],
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
