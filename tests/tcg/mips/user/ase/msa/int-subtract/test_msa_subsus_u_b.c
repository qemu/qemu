/*
 *  Test program for MSA instruction SUBSUS_U.B
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
    char *instruction_name =  "SUBSUS_U.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xffffc7ffffc7ffffULL, 0xc7ffffc7ffffc7ffULL, },
        { 0xe38effe38effe38eULL, 0xffe38effe38effe3ULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5656565656565656ULL, 0x5656565656565656ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x3434343434343434ULL, 0x3434343434343434ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x1d72001d72001d72ULL, 0x001d72001d72001dULL, },
        { 0x0000390000390000ULL, 0x3900003900003900ULL, },
        { 0xababababababababULL, 0xababababababababULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xdedededededededeULL, 0xdedededededededeULL, },
        { 0x7777777777777777ULL, 0x7777777777777777ULL, },
        { 0xc7ff72c7ff72c7ffULL, 0x72c7ff72c7ff72c7ULL, },
        { 0x8e39e38e39e38e39ULL, 0xe38e39e38e39e38eULL, },
        { 0x5656565656565656ULL, 0x5656565656565656ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xababababababababULL, 0xababababababababULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x8989898989898989ULL, 0x8989898989898989ULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x72c71d72c71d72c7ULL, 0x1d72c71d72c71d72ULL, },
        { 0x39008e39008e3900ULL, 0x8e39008e39008e39ULL, },
        { 0xcdcdcdcdcdcdcdcdULL, 0xcdcdcdcdcdcdcdcdULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x7777777777777777ULL, 0x7777777777777777ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x9999999999999999ULL, 0x9999999999999999ULL, },
        { 0xe9ff94e9ff94e9ffULL, 0x94e9ff94e9ff94e9ULL, },
        { 0xb05bffb05bffb05bULL, 0xffb05bffb05bffb0ULL, },
        { 0x3434343434343434ULL, 0x3434343434343434ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x8989898989898989ULL, 0x8989898989898989ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x6767676767676767ULL, 0x6767676767676767ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x50a50050a50050a5ULL, 0x0050a50050a50050ULL, },
        { 0x17006c17006c1700ULL, 0x6c17006c17006c17ULL, },
        { 0xe48f39e48f39e48fULL, 0x39e48f39e48f39e4ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xffe48effe48effe4ULL, 0x8effe48effe48effULL, },
        { 0x8e39008e39008e39ULL, 0x008e39008e39008eULL, },
        { 0xffc26cffc26cffc2ULL, 0x6cffc26cffc26cffULL, },
        { 0xb05b05b05b05b05bULL, 0x05b05b05b05b05b0ULL, },
        { 0xffff00ffff00ffffULL, 0x00ffff00ffff00ffULL, },
        { 0xc71d71c71d71c71dULL, 0x71c71d71c71d71c7ULL, },
        { 0x1d72c81d72c81d72ULL, 0xc81d72c81d72c81dULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x72c7ff72c7ff72c7ULL, 0xff72c7ff72c7ff72ULL, },
        { 0x001c72001c72001cULL, 0x72001c72001c7200ULL, },
        { 0x50a5fb50a5fb50a5ULL, 0xfb50a5fb50a5fb50ULL, },
        { 0x003e94003e94003eULL, 0x94003e94003e9400ULL, },
        { 0x39e38f39e38f39e3ULL, 0x8f39e38f39e38f39ULL, },
        { 0x0000ff0000ff0000ULL, 0xff0000ff0000ff00ULL, },
        { 0xff00ffff00000000ULL, 0x00000000ff00ff00ULL, },    /*  64  */
        { 0x8dace66900cf8e38ULL, 0x39705044e93c5e10ULL, },
        { 0xdc10ffff6f93cac0ULL, 0x248f455fff508b00ULL, },
        { 0x181bd07f00317300ULL, 0xbe768386ff39ce6cULL, },
        { 0xff541a9725317200ULL, 0x0090b0001700a2f0ULL, },
        { 0xffff000000ffff00ULL, 0x00ffff00000000ffULL, },
        { 0xff6452b994c4ff88ULL, 0x00fff51b6a142de8ULL, },
        { 0x8b6f00160062e500ULL, 0x85ffff426c0070ffULL, },
        { 0xff00c8de916d3640ULL, 0x0071bba1ad007508ULL, },    /*  72  */
        { 0xb19cae476cffc478ULL, 0x15e1ffe596000018ULL, },
        { 0xff00ffffffffffffULL, 0x00ffffffff000000ULL, },
        { 0x3c0b985d5b9ea932ULL, 0x9ae7ffffff004374ULL, },
        { 0xe800308136008d0eULL, 0x428a7d7aab00ff94ULL, },
        { 0x75911600119eff46ULL, 0x7bfacdbe940390a4ULL, },
        { 0xc40068a3a562ffceULL, 0x66ffc2d9fe17bd8cULL, },
        { 0x000000000000ff00ULL, 0xffffffffff00ffffULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_SUBSUS_U_B(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_SUBSUS_U_B(b128_random[i], b128_random[j],
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
