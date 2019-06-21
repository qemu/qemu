/*
 *  Test program for MSA instruction ADDS_U.H
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
    char *instruction_name =  "ADDS_U.H";
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
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xddddddddddddddddULL, 0xddddddddddddddddULL, },
        { 0xffffe38dffffffffULL, 0xe38dffffffffe38dULL, },
        { 0xc71bffffffffc71bULL, 0xffffffffc71bffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x8888888888888888ULL, 0x8888888888888888ULL, },
        { 0xffff8e38e38dffffULL, 0x8e38e38dffff8e38ULL, },
        { 0x71c6ffffc71c71c6ULL, 0xffffc71c71c6ffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xe93dffffffffe93dULL, 0xffffffffe93dffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xddddddddddddddddULL, 0xddddddddddddddddULL, },
        { 0x8888888888888888ULL, 0x8888888888888888ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x6666666666666666ULL, 0x6666666666666666ULL, },
        { 0xffff6c16c16bffffULL, 0x6c16c16bffff6c16ULL, },
        { 0x4fa4fa4fa4fa4fa4ULL, 0xfa4fa4fa4fa4fa4fULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xffffe38dffffffffULL, 0xe38dffffffffe38dULL, },
        { 0xffff8e38e38dffffULL, 0x8e38e38dffff8e38ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffff6c16c16bffffULL, 0x6c16c16bffff6c16ULL, },
        { 0xffff71c6ffffffffULL, 0x71c6ffffffff71c6ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0xc71bffffffffc71bULL, 0xffffffffc71bffffULL, },
        { 0x71c6ffffc71c71c6ULL, 0xffffc71c71c6ffffULL, },
        { 0xe93dffffffffe93dULL, 0xffffffffe93dffffULL, },
        { 0x4fa4fa4fa4fa4fa4ULL, 0xfa4fa4fa4fa4fa4fULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x38e2ffffe38e38e2ULL, 0xffffe38e38e2ffffULL, },
        { 0xffffffff50c4aa80ULL, 0x96ce16bcffffffffULL, },    /*  64  */
        { 0xffffe72f75f5ffffULL, 0x5e5ec678ffffffffULL, },
        { 0xffffffffe231e0c0ULL, 0x733fd25dffffd520ULL, },
        { 0xf8b9fd198693ffffULL, 0xd9589436ffffffffULL, },
        { 0xffffe72f75f5ffffULL, 0x5e5ec678ffffffffULL, },
        { 0xffff00c69b26ffffULL, 0x25eeffff2a7ea5f8ULL, },
        { 0xffffaf0dffffffffULL, 0x3acfffffc06a7810ULL, },
        { 0xffff16b0abc4ffffULL, 0xa0e8ffffbe81ffffULL, },
        { 0xffffffffe231e0c0ULL, 0x733fd25dffffd520ULL, },    /*  72  */
        { 0xffffaf0dffffffffULL, 0x3acfffffc06a7810ULL, },
        { 0xffffffffffffffffULL, 0x4fb0ffffffff4a28ULL, },
        { 0xffffc4f7ffffffffULL, 0xb5c9ffffffffffffULL, },
        { 0xf8b9fd198693ffffULL, 0xd9589436ffffffffULL, },
        { 0xffff16b0abc4ffffULL, 0xa0e8ffffbe81ffffULL, },
        { 0xffffc4f7ffffffffULL, 0xb5c9ffffffffffffULL, },
        { 0xe09e2c9abc62ffffULL, 0xffffffffffffffffULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_ADDS_U_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_ADDS_U_H(b128_random[i], b128_random[j],
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
