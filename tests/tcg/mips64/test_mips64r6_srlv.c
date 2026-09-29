/*
 *  Test program for MIPS64R6 instruction SRLV
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
    char *instruction_name =   "SRLV";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b64_result[TEST_COUNT_TOTAL];
    uint64_t b64_expect[TEST_COUNT_TOTAL] = {
        0x0000000000000001ULL,                    /*   0  */
        0xffffffffffffffffULL,
        0x00000000003fffffULL,
        0x00000000000007ffULL,
        0x00000000000fffffULL,
        0x0000000000001fffULL,
        0x000000000003ffffULL,
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
        0xffffffffaaaaaaaaULL,
        0x00000000002aaaaaULL,
        0x0000000000000555ULL,
        0x00000000000aaaaaULL,
        0x0000000000001555ULL,
        0x000000000002aaaaULL,
        0x0000000000005555ULL,
        0x0000000000000000ULL,                    /*  24  */
        0x0000000055555555ULL,
        0x0000000000155555ULL,
        0x00000000000002aaULL,
        0x0000000000055555ULL,
        0x0000000000000aaaULL,
        0x0000000000015555ULL,
        0x0000000000002aaaULL,
        0x0000000000000001ULL,                    /*  32  */
        0xffffffffccccccccULL,
        0x0000000000333333ULL,
        0x0000000000000666ULL,
        0x00000000000cccccULL,
        0x0000000000001999ULL,
        0x0000000000033333ULL,
        0x0000000000006666ULL,
        0x0000000000000000ULL,                    /*  40  */
        0x0000000033333333ULL,
        0x00000000000cccccULL,
        0x0000000000000199ULL,
        0x0000000000033333ULL,
        0x0000000000000666ULL,
        0x000000000000ccccULL,
        0x0000000000001999ULL,
        0x0000000000000001ULL,                    /*  48  */
        0xffffffff8e38e38eULL,
        0x0000000000238e38ULL,
        0x0000000000000471ULL,
        0x000000000008e38eULL,
        0x00000000000011c7ULL,
        0x00000000000238e3ULL,
        0x000000000000471cULL,
        0x0000000000000000ULL,                    /*  56  */
        0x0000000071c71c71ULL,
        0x00000000001c71c7ULL,
        0x000000000000038eULL,
        0x0000000000071c71ULL,
        0x0000000000000e38ULL,
        0x000000000001c71cULL,
        0x00000000000038e3ULL,
        0x0000000028625540ULL,                    /*  64  */
        0x0000000000286255ULL,
        0x0000000028625540ULL,
        0x000000000000a189ULL,
        0x000000004d93c708ULL,
        0x00000000004d93c7ULL,
        0x000000004d93c708ULL,
        0x000000000001364fULL,
        0xffffffffb9cf8b80ULL,                    /*  72  */
        0x0000000000b9cf8bULL,
        0xffffffffb9cf8b80ULL,
        0x000000000002e73eULL,
        0x000000005e31e24eULL,
        0x00000000005e31e2ULL,
        0x000000005e31e24eULL,
        0x00000000000178c7ULL,
    };

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_SRLV(b64_pattern + i, b64_pattern + j,
                b64_result + (PATTERN_INPUTS_64_SHORT_COUNT * i + j));
        }
    }

    for (i = 0; i < RANDOM_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_SRLV(b64_random + i, b64_random + j,
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
