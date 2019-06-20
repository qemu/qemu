/*
 *  Test program for MSA instruction SUBSUU_S.B
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
    char *instruction_name =  "SUBSUU_S.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x1c717f1c717f1c71ULL, 0x7f1c717f1c717f1cULL, },
        { 0x7f7f387f7f387f7fULL, 0x387f7f387f7f387fULL, },
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },
        { 0xababababababababULL, 0xababababababababULL, },
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },
        { 0xcdcdcdcdcdcdcdcdULL, 0xcdcdcdcdcdcdcdcdULL, },
        { 0x8080c88080c88080ULL, 0xc88080c88080c880ULL, },
        { 0xe48f80e48f80e48fULL, 0x80e48f80e48f80e4ULL, },
        { 0xababababababababULL, 0xababababababababULL, },    /*  16  */
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xdedededededededeULL, 0xdedededededededeULL, },
        { 0x7777777777777777ULL, 0x7777777777777777ULL, },
        { 0xc71c72c71c72c71cULL, 0x72c71c72c71c72c7ULL, },
        { 0x7f39e37f39e37f39ULL, 0xe37f39e37f39e37fULL, },
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xababababababababULL, 0xababababababababULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x8989898989898989ULL, 0x8989898989898989ULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x80c71d80c71d80c7ULL, 0x1d80c71d80c71d80ULL, },
        { 0x39e48e39e48e39e4ULL, 0x8e39e48e39e48e39ULL, },
        { 0xcdcdcdcdcdcdcdcdULL, 0xcdcdcdcdcdcdcdcdULL, },    /*  32  */
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x7777777777777777ULL, 0x7777777777777777ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0xe93e7fe93e7fe93eULL, 0x7fe93e7fe93e7fe9ULL, },
        { 0x7f5b057f5b057f5bULL, 0x057f5b057f5b057fULL, },
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x8989898989898989ULL, 0x8989898989898989ULL, },
        { 0xdedededededededeULL, 0xdedededededededeULL, },
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x80a5fb80a5fb80a5ULL, 0xfb80a5fb80a5fb80ULL, },
        { 0x17c28017c28017c2ULL, 0x8017c28017c28017ULL, },
        { 0xe48f80e48f80e48fULL, 0x80e48f80e48f80e4ULL, },    /*  48  */
        { 0x7f7f387f7f387f7fULL, 0x387f7f387f7f387fULL, },
        { 0x39e48e39e48e39e4ULL, 0x8e39e48e39e48e39ULL, },
        { 0x7f39e37f39e37f39ULL, 0xe37f39e37f39e37fULL, },
        { 0x17c28017c28017c2ULL, 0x8017c28017c28017ULL, },
        { 0x7f5b057f5b057f5bULL, 0x057f5b057f5b057fULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x7f1d807f1d807f1dULL, 0x807f1d807f1d807fULL, },
        { 0x8080c88080c88080ULL, 0xc88080c88080c880ULL, },    /*  56  */
        { 0x1c717f1c717f1c71ULL, 0x7f1c717f1c717f1cULL, },
        { 0x80c71d80c71d80c7ULL, 0x1d80c71d80c71d80ULL, },
        { 0xc71c72c71c72c71cULL, 0x72c71c72c71c72c7ULL, },
        { 0x80a5fb80a5fb80a5ULL, 0xfb80a5fb80a5fb80ULL, },
        { 0xe93e7fe93e7fe93eULL, 0x7fe93e7fe93e7fe9ULL, },
        { 0x80e37f80e37f80e3ULL, 0x7f80e37f80e37f80ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  64  */
        { 0x8dac7f69dbcf8e38ULL, 0x398080447f3c5e80ULL, },
        { 0xdc1038228093cac0ULL, 0x248f808053507ff8ULL, },
        { 0x181b7f7fca3180f2ULL, 0xbe8083865539ce80ULL, },
        { 0x73548097253172c8ULL, 0xc77f7fbc80c4a27fULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x4f6480b994c43c88ULL, 0xeb1ff58080142d7fULL, },
        { 0x7f6fea16ef62e5baULL, 0x8506338080fd805cULL, },
        { 0x24f0c8de7f6d3640ULL, 0xdc717f7fadb08008ULL, },    /*  72  */
        { 0xb19c7f476c3cc478ULL, 0x15e10b7f7fecd380ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x3c0b7f5d5b7fa932ULL, 0x9ae73e2702e98080ULL, },
        { 0xe8e5808136cf7f0eULL, 0x427f7d7aabc7327fULL, },
        { 0x809116ea119e1b46ULL, 0x7bfacd7f7f037fa4ULL, },
        { 0xc4f580a3a58057ceULL, 0x6619c2d9fe177f7fULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_SUBSUU_S_B(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_SUBSUU_S_B(b128_random[i], b128_random[j],
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
