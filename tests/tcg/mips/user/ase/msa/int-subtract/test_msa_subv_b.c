/*
 *  Test program for MSA instruction SUBV.B
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
    char *instruction_name =  "SUBV.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5656565656565656ULL, 0x5656565656565656ULL, },
        { 0xababababababababULL, 0xababababababababULL, },
        { 0x3434343434343434ULL, 0x3434343434343434ULL, },
        { 0xcdcdcdcdcdcdcdcdULL, 0xcdcdcdcdcdcdcdcdULL, },
        { 0x1d72c81d72c81d72ULL, 0xc81d72c81d72c81dULL, },
        { 0xe48f39e48f39e48fULL, 0x39e48f39e48f39e4ULL, },
        { 0xababababababababULL, 0xababababababababULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xdedededededededeULL, 0xdedededededededeULL, },
        { 0x7777777777777777ULL, 0x7777777777777777ULL, },
        { 0xc71c72c71c72c71cULL, 0x72c71c72c71c72c7ULL, },
        { 0x8e39e38e39e38e39ULL, 0xe38e39e38e39e38eULL, },
        { 0x5656565656565656ULL, 0x5656565656565656ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xababababababababULL, 0xababababababababULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x8989898989898989ULL, 0x8989898989898989ULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x72c71d72c71d72c7ULL, 0x1d72c71d72c71d72ULL, },
        { 0x39e48e39e48e39e4ULL, 0x8e39e48e39e48e39ULL, },
        { 0xcdcdcdcdcdcdcdcdULL, 0xcdcdcdcdcdcdcdcdULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x7777777777777777ULL, 0x7777777777777777ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x9999999999999999ULL, 0x9999999999999999ULL, },
        { 0xe93e94e93e94e93eULL, 0x94e93e94e93e94e9ULL, },
        { 0xb05b05b05b05b05bULL, 0x05b05b05b05b05b0ULL, },
        { 0x3434343434343434ULL, 0x3434343434343434ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x8989898989898989ULL, 0x8989898989898989ULL, },
        { 0xdedededededededeULL, 0xdedededededededeULL, },
        { 0x6767676767676767ULL, 0x6767676767676767ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x50a5fb50a5fb50a5ULL, 0xfb50a5fb50a5fb50ULL, },
        { 0x17c26c17c26c17c2ULL, 0x6c17c26c17c26c17ULL, },
        { 0xe48f39e48f39e48fULL, 0x39e48f39e48f39e4ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x39e48e39e48e39e4ULL, 0x8e39e48e39e48e39ULL, },
        { 0x8e39e38e39e38e39ULL, 0xe38e39e38e39e38eULL, },
        { 0x17c26c17c26c17c2ULL, 0x6c17c26c17c26c17ULL, },
        { 0xb05b05b05b05b05bULL, 0x05b05b05b05b05b0ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xc71d71c71d71c71dULL, 0x71c71d71c71d71c7ULL, },
        { 0x1d72c81d72c81d72ULL, 0xc81d72c81d72c81dULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x72c71d72c71d72c7ULL, 0x1d72c71d72c71d72ULL, },
        { 0xc71c72c71c72c71cULL, 0x72c71c72c71c72c7ULL, },
        { 0x50a5fb50a5fb50a5ULL, 0xfb50a5fb50a5fb50ULL, },
        { 0xe93e94e93e94e93eULL, 0x94e93e94e93e94e9ULL, },
        { 0x39e38f39e38f39e3ULL, 0x8f39e38f39e38f39ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  64  */
        { 0x8dace669dbcf8e38ULL, 0x39705044e93c5e10ULL, },
        { 0xdc1038226f93cac0ULL, 0x248f455f53508bf8ULL, },
        { 0x181bd07fca3173f2ULL, 0xbe7683865539ce6cULL, },
        { 0x73541a97253172c8ULL, 0xc790b0bc17c4a2f0ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x4f6452b994c43c88ULL, 0xeb1ff51b6a142de8ULL, },
        { 0x8b6fea16ef62e5baULL, 0x850633426cfd705cULL, },
        { 0x24f0c8de916d3640ULL, 0xdc71bba1adb07508ULL, },    /*  72  */
        { 0xb19cae476c3cc478ULL, 0x15e10be596ecd318ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x3c0b985d5b9ea932ULL, 0x9ae73e2702e94374ULL, },
        { 0xe8e5308136cf8d0eULL, 0x428a7d7aabc73294ULL, },
        { 0x759116ea119e1b46ULL, 0x7bfacdbe940390a4ULL, },
        { 0xc4f568a3a56257ceULL, 0x6619c2d9fe17bd8cULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_SUBV_B(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_SUBV_B(b128_random[i], b128_random[j],
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
