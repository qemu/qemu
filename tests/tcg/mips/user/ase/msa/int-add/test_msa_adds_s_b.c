/*
 *  Test program for MSA instruction ADDS_S.B
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
    char *instruction_name =  "ADDS_S.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xfefefefefefefefeULL, 0xfefefefefefefefeULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xa9a9a9a9a9a9a9a9ULL, 0xa9a9a9a9a9a9a9a9ULL, },
        { 0x5454545454545454ULL, 0x5454545454545454ULL, },
        { 0xcbcbcbcbcbcbcbcbULL, 0xcbcbcbcbcbcbcbcbULL, },
        { 0x3232323232323232ULL, 0x3232323232323232ULL, },
        { 0xe28d37e28d37e28dULL, 0x37e28d37e28d37e2ULL, },
        { 0x1b70c61b70c61b70ULL, 0xc61b70c61b70c61bULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0xa9a9a9a9a9a9a9a9ULL, 0xa9a9a9a9a9a9a9a9ULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },
        { 0xddddddddddddddddULL, 0xddddddddddddddddULL, },
        { 0x8d80e28d80e28d80ULL, 0xe28d80e28d80e28dULL, },
        { 0xc61b80c61b80c61bULL, 0x80c61b80c61b80c6ULL, },
        { 0x5454545454545454ULL, 0x5454545454545454ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x2121212121212121ULL, 0x2121212121212121ULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x38e37f38e37f38e3ULL, 0x7f38e37f38e37f38ULL, },
        { 0x717f1c717f1c717fULL, 0x1c717f1c717f1c71ULL, },
        { 0xcbcbcbcbcbcbcbcbULL, 0xcbcbcbcbcbcbcbcbULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },
        { 0x2121212121212121ULL, 0x2121212121212121ULL, },
        { 0x9898989898989898ULL, 0x9898989898989898ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xaf8004af8004af80ULL, 0x04af8004af8004afULL, },
        { 0xe83d93e83d93e83dULL, 0x93e83d93e83d93e8ULL, },
        { 0x3232323232323232ULL, 0x3232323232323232ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xddddddddddddddddULL, 0xddddddddddddddddULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x6666666666666666ULL, 0x6666666666666666ULL, },
        { 0x16c16b16c16b16c1ULL, 0x6b16c16b16c16b16ULL, },
        { 0x4f7ffa4f7ffa4f7fULL, 0xfa4f7ffa4f7ffa4fULL, },
        { 0xe28d37e28d37e28dULL, 0x37e28d37e28d37e2ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x8d80e28d80e28d80ULL, 0xe28d80e28d80e28dULL, },
        { 0x38e37f38e37f38e3ULL, 0x7f38e37f38e37f38ULL, },
        { 0xaf8004af8004af80ULL, 0x04af8004af8004afULL, },
        { 0x16c16b16c16b16c1ULL, 0x6b16c16b16c16b16ULL, },
        { 0xc68070c68070c680ULL, 0x70c68070c68070c6ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x1b70c61b70c61b70ULL, 0xc61b70c61b70c61bULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0xc61b80c61b80c61bULL, 0x80c61b80c61b80c6ULL, },
        { 0x717f1c717f1c717fULL, 0x1c717f1c717f1c71ULL, },
        { 0xe83d93e83d93e83dULL, 0x93e83d93e83d93e8ULL, },
        { 0x4f7ffa4f7ffa4f7fULL, 0xfa4f7ffa4f7ffa4fULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x387f8e387f8e387fULL, 0x8e387f8e387f8e38ULL, },
        { 0x807fcc98507f7f7fULL, 0x7f7f167ffc7f8018ULL, },    /*  64  */
        { 0x8328e62f75f51c48ULL, 0x5d5ec678137f0208ULL, },
        { 0x807f9480e131e0c0ULL, 0x723fd15da97fd520ULL, },
        { 0xf87ffc197f7f377fULL, 0xd8589336a77f92acULL, },
        { 0x8328e62f75f51c48ULL, 0x5d5ec678137f0208ULL, },
        { 0xf680007f7f808e10ULL, 0x24ee80342a7e7ff8ULL, },
        { 0xa718ae0d06808088ULL, 0x39cf8119c06a7710ULL, },
        { 0x6b0d167f7fc4a956ULL, 0x9fe880f2be7f349cULL, },
        { 0x807f9480e131e0c0ULL, 0x723fd15da97fd520ULL, },    /*  72  */
        { 0xa718ae0d06808088ULL, 0x39cf8119c06a7710ULL, },
        { 0x807f8080809e8080ULL, 0x4eb08cfe80564a28ULL, },
        { 0x1c7fc4f7170080ceULL, 0xb4c980d7806d07b4ULL, },
        { 0xf87ffc197f7f377fULL, 0xd8589336a77f92acULL, },
        { 0x6b0d167f7fc4a956ULL, 0x9fe880f2be7f349cULL, },
        { 0x1c7fc4f7170080ceULL, 0xb4c980d7806d07b4ULL, },
        { 0x7f7f2c7f7f62c47fULL, 0x80e280b0807fc480ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_ADDS_S_B(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_ADDS_S_B(b128_random[i], b128_random[j],
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
