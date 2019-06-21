/*
 *  Test program for MSA instruction MOD_U.W
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
    char *group_name = "Int Modulo";
    char *instruction_name =  "MOD_U.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x1c71c71c71c71c71ULL, 0x1c71c71f1c71c71cULL, },
        { 0x000000031c71c71dULL, 0x38e38e3800000003ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x1111111111111111ULL, 0x1111111111111111ULL, },
        { 0xaaaaaaaa1c71c71cULL, 0x00000002aaaaaaaaULL, },
        { 0x0000000238e38e39ULL, 0xaaaaaaaa00000002ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x5555555555555555ULL, 0x1c71c71d55555555ULL, },
        { 0x0000000155555555ULL, 0x5555555500000001ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xcccccccc3e93e93eULL, 0x22222224ccccccccULL, },
        { 0x05b05b085b05b05bULL, 0x05b05b0505b05b08ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x16c16c1733333333ULL, 0x3333333316c16c17ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x38e38e398e38e38eULL, 0x38e38e3838e38e39ULL, },
        { 0x38e38e3938e38e39ULL, 0x38e38e3838e38e39ULL, },
        { 0x16c16c178e38e38eULL, 0x38e38e3816c16c17ULL, },
        { 0x16c16c1727d27d28ULL, 0x05b05b0516c16c17ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x000000031c71c71dULL, 0x38e38e3800000003ULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0x1c71c71d1c71c71cULL, },
        { 0x1c71c71c1c71c71cULL, 0x1c71c71d1c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c71c71c0b60b60bULL, 0x2d82d82e1c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0x1c71c71f1c71c71cULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  64  */
        { 0x886ae6cc28625540ULL, 0x127fda1014c31f38ULL, },
        { 0x886ae6cc28625540ULL, 0x238e445f53508af8ULL, },
        { 0x181bd07f28625540ULL, 0x4b670b5e5538cd6cULL, },
        { 0x73531997253171c8ULL, 0x12f7bb1a153f52fcULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x4f6351b94d93c708ULL, 0x12f7bb1a153f52fcULL, },
        { 0x1b1fd3c94d93c708ULL, 0x12f7bb1a153f52fcULL, },
        { 0x23efc7de18463680ULL, 0x27d8c6ffab2b2514ULL, },    /*  72  */
        { 0xac5aaeaa1ea7fd70ULL, 0x01e950cb01308d34ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x3c0b985d5b9da932ULL, 0x27d8c6ff01e84274ULL, },
        { 0x704f164d0d6d37ceULL, 0x428a7d7aa942e2a0ULL, },
        { 0x704f164d109e1b46ULL, 0x092b6b2214879dbcULL, },
        { 0x704f164d5e31e24eULL, 0x166733dba942e2a0ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MOD_U_W(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MOD_U_W(b128_random[i], b128_random[j],
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
