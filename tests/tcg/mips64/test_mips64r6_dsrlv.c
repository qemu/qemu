/*
 *  Test program for MIPS64R6 instruction DSRLV
 *
 *  Copyright (C) 2019  Wave Computing, Inc.
 *  Copyright (C) 2019  Aleksandar Markovic <amarkovic@wavecomp.com>
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

#include "../../../../include/wrappers_mips64r6.h"
#include "../../../../include/test_inputs_64.h"
#include "../../../../include/test_utils_64.h"

#define TEST_COUNT_TOTAL (PATTERN_INPUTS_64_COUNT + RANDOM_INPUTS_64_COUNT)


int32_t main(void)
{
    char *isa_ase_name = "mips64r6";
    char *group_name = "Shift";
    char *instruction_name =   "DSRLV";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b64_result[TEST_COUNT_TOTAL];
    uint64_t b64_expect[TEST_COUNT_TOTAL] = {
        0x0000000000000001ULL,                    /*   0  */
        0xffffffffffffffffULL,
        0x00000000003fffffULL,
        0x000007ffffffffffULL,
        0x000fffffffffffffULL,
        0x0000000000001fffULL,
        0x0003ffffffffffffULL,
        0x0000000000007fffULL,
        0x0000000000000000ULL,                    /*   8  */
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000001ULL,                    /*  16  */
        0xaaaaaaaaaaaaaaaaULL,
        0x00000000002aaaaaULL,
        0x0000055555555555ULL,
        0x000aaaaaaaaaaaaaULL,
        0x0000000000001555ULL,
        0x0002aaaaaaaaaaaaULL,
        0x0000000000005555ULL,
        0x0000000000000000ULL,                    /*  24  */
        0x5555555555555555ULL,
        0x0000000000155555ULL,
        0x000002aaaaaaaaaaULL,
        0x0005555555555555ULL,
        0x0000000000000aaaULL,
        0x0001555555555555ULL,
        0x0000000000002aaaULL,
        0x0000000000000001ULL,                    /*  32  */
        0xccccccccccccccccULL,
        0x0000000000333333ULL,
        0x0000066666666666ULL,
        0x000cccccccccccccULL,
        0x0000000000001999ULL,
        0x0003333333333333ULL,
        0x0000000000006666ULL,
        0x0000000000000000ULL,                    /*  40  */
        0x3333333333333333ULL,
        0x00000000000cccccULL,
        0x0000019999999999ULL,
        0x0003333333333333ULL,
        0x0000000000000666ULL,
        0x0000ccccccccccccULL,
        0x0000000000001999ULL,
        0x0000000000000001ULL,                    /*  48  */
        0xe38e38e38e38e38eULL,
        0x000000000038e38eULL,
        0x0000071c71c71c71ULL,
        0x000e38e38e38e38eULL,
        0x0000000000001c71ULL,
        0x00038e38e38e38e3ULL,
        0x00000000000071c7ULL,
        0x0000000000000000ULL,                    /*  56  */
        0x1c71c71c71c71c71ULL,
        0x0000000000071c71ULL,
        0x000000e38e38e38eULL,
        0x0001c71c71c71c71ULL,
        0x000000000000038eULL,
        0x000071c71c71c71cULL,
        0x0000000000000e38ULL,
        0x886ae6cc28625540ULL,                    /*  64  */
        0x00886ae6cc286255ULL,
        0x886ae6cc28625540ULL,
        0x000221ab9b30a189ULL,
        0xfbbe00634d93c708ULL,
        0x00fbbe00634d93c7ULL,
        0xfbbe00634d93c708ULL,
        0x0003eef8018d364fULL,
        0xac5aaeaab9cf8b80ULL,                    /*  72  */
        0x00ac5aaeaab9cf8bULL,
        0xac5aaeaab9cf8b80ULL,
        0x0002b16abaaae73eULL,
        0x704f164d5e31e24eULL,
        0x00704f164d5e31e2ULL,
        0x704f164d5e31e24eULL,
        0x0001c13c593578c7ULL,
    };

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_DSRLV(b64_pattern + i, b64_pattern + j,
                b64_result + (PATTERN_INPUTS_64_SHORT_COUNT * i + j));
        }
    }

    for (i = 0; i < RANDOM_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_DSRLV(b64_random + i, b64_random + j,
                b64_result + (((PATTERN_INPUTS_64_SHORT_COUNT) *
                               (PATTERN_INPUTS_64_SHORT_COUNT)) +
                              RANDOM_INPUTS_64_SHORT_COUNT * i + j));
        }
    }

    gettimeofday(&end, NULL);

    elapsed_time = (end.tv_sec - start.tv_sec) * 1000.0;
    elapsed_time += (end.tv_usec - start.tv_usec) / 1000.0;

    ret = check_results_64(instruction_name, TEST_COUNT_TOTAL, elapsed_time,
                           b64_result, b64_expect);

    return ret;
}
