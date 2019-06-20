/*
 *  Test program for MSA instruction SUBSUU_S.H
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
    char *instruction_name =  "SUBSUU_S.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0x7fff7fff7fff7fffULL, 0x7fff7fff7fff7fffULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x7fff7fff7fff7fffULL, 0x7fff7fff7fff7fffULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x7fff7fff7fff7fffULL, 0x7fff7fff7fff7fffULL, },
        { 0x1c717fff71c71c71ULL, 0x7fff71c71c717fffULL, },
        { 0x7fff38e37fff7fffULL, 0x38e37fff7fff38e3ULL, },
        { 0x8000800080008000ULL, 0x8000800080008000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x8000800080008000ULL, 0x8000800080008000ULL, },
        { 0xaaabaaabaaabaaabULL, 0xaaabaaabaaabaaabULL, },
        { 0x8000800080008000ULL, 0x8000800080008000ULL, },
        { 0xcccdcccdcccdcccdULL, 0xcccdcccdcccdcccdULL, },
        { 0x8000c71d80008000ULL, 0xc71d80008000c71dULL, },
        { 0xe38f80008e39e38fULL, 0x80008e39e38f8000ULL, },
        { 0xaaabaaabaaabaaabULL, 0xaaabaaabaaabaaabULL, },    /*  16  */
        { 0x7fff7fff7fff7fffULL, 0x7fff7fff7fff7fffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xdddedddedddedddeULL, 0xdddedddedddedddeULL, },
        { 0x7777777777777777ULL, 0x7777777777777777ULL, },
        { 0xc71c71c71c72c71cULL, 0x71c71c72c71c71c7ULL, },
        { 0x7fffe38e38e37fffULL, 0xe38e38e37fffe38eULL, },
        { 0x8000800080008000ULL, 0x8000800080008000ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xaaabaaabaaabaaabULL, 0xaaabaaabaaabaaabULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x8889888988898889ULL, 0x8889888988898889ULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x80001c72c71d8000ULL, 0x1c72c71d80001c72ULL, },
        { 0x38e48e39e38e38e4ULL, 0x8e39e38e38e48e39ULL, },
        { 0xcccdcccdcccdcccdULL, 0xcccdcccdcccdcccdULL, },    /*  32  */
        { 0x7fff7fff7fff7fffULL, 0x7fff7fff7fff7fffULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x7777777777777777ULL, 0x7777777777777777ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x7fff7fff7fff7fffULL, 0x7fff7fff7fff7fffULL, },
        { 0xe93e7fff3e94e93eULL, 0x7fff3e94e93e7fffULL, },
        { 0x7fff05b05b057fffULL, 0x05b05b057fff05b0ULL, },
        { 0x8000800080008000ULL, 0x8000800080008000ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x8889888988898889ULL, 0x8889888988898889ULL, },
        { 0xdddedddedddedddeULL, 0xdddedddedddedddeULL, },
        { 0x8000800080008000ULL, 0x8000800080008000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x8000fa50a4fb8000ULL, 0xfa50a4fb8000fa50ULL, },
        { 0x16c28000c16c16c2ULL, 0x8000c16c16c28000ULL, },
        { 0xe38f80008e39e38fULL, 0x80008e39e38f8000ULL, },    /*  48  */
        { 0x7fff38e37fff7fffULL, 0x38e37fff7fff38e3ULL, },
        { 0x38e48e39e38e38e4ULL, 0x8e39e38e38e48e39ULL, },
        { 0x7fffe38e38e37fffULL, 0xe38e38e37fffe38eULL, },
        { 0x16c28000c16c16c2ULL, 0x8000c16c16c28000ULL, },
        { 0x7fff05b05b057fffULL, 0x05b05b057fff05b0ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x7fff80001c717fffULL, 0x80001c717fff8000ULL, },
        { 0x8000c71d80008000ULL, 0xc71d80008000c71dULL, },    /*  56  */
        { 0x1c717fff71c71c71ULL, 0x7fff71c71c717fffULL, },
        { 0x80001c72c71d8000ULL, 0x1c72c71d80001c72ULL, },
        { 0xc71c71c71c72c71cULL, 0x71c71c72c71c71c7ULL, },
        { 0x8000fa50a4fb8000ULL, 0xfa50a4fb8000fa50ULL, },
        { 0xe93e7fff3e94e93eULL, 0x7fff3e94e93e7fffULL, },
        { 0x80007fffe38f8000ULL, 0x7fffe38f80007fffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  64  */
        { 0x8cac7fffdacf8e38ULL, 0x387080007fff5d10ULL, },
        { 0xdc1038228000c9c0ULL, 0x238f800053507fffULL, },
        { 0x181b7fffca318000ULL, 0xbd7682865539cd6cULL, },
        { 0x73548000253171c8ULL, 0xc7907fff8000a2f0ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x4f64800093c43b88ULL, 0xeb1ff41b80002de8ULL, },
        { 0x7fffea16ef62e4baULL, 0x8506324280008000ULL, },
        { 0x23f0c7de7fff3640ULL, 0xdc717fffacb08000ULL, },    /*  72  */
        { 0xb09c7fff6c3cc478ULL, 0x14e10be57fffd218ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x3c0b7fff5b9ea932ULL, 0x99e73e2701e98000ULL, },
        { 0xe7e5800035cf7fffULL, 0x428a7d7aaac73294ULL, },
        { 0x800015ea109e1b46ULL, 0x7afacdbe7fff7fffULL, },
        { 0xc3f58000a46256ceULL, 0x6619c1d9fe177fffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_SUBSUU_S_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_SUBSUU_S_H(b128_random[i], b128_random[j],
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
