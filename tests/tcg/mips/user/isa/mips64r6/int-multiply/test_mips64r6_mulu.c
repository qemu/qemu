/*
 *  Test program for MIPS64R6 instruction MULU
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
    char *group_name = "Int Multiply";
    char *instruction_name =   "MULU";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b64_result[TEST_COUNT_TOTAL];
    uint64_t b64_expect[TEST_COUNT_TOTAL] = {
        0x0000000000000001ULL,                    /*   0  */
        0x0000000000000000ULL,
        0x0000000055555556ULL,
        0xffffffffaaaaaaabULL,
        0x0000000033333334ULL,
        0xffffffffcccccccdULL,
        0x0000000071c71c72ULL,
        0xffffffff8e38e38fULL,
        0x0000000000000000ULL,                    /*   8  */
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000055555556ULL,                    /*  16  */
        0x0000000000000000ULL,
        0xffffffffe38e38e4ULL,
        0x0000000071c71c72ULL,
        0x0000000077777778ULL,
        0xffffffffdddddddeULL,
        0xffffffffa12f684cULL,
        0xffffffffb425ed0aULL,
        0xffffffffaaaaaaabULL,                    /*  24  */
        0x0000000000000000ULL,
        0x0000000071c71c72ULL,
        0x0000000038e38e39ULL,
        0xffffffffbbbbbbbcULL,
        0xffffffffeeeeeeefULL,
        0xffffffffd097b426ULL,
        0xffffffffda12f685ULL,
        0x0000000033333334ULL,                    /*  32  */
        0x0000000000000000ULL,
        0x0000000077777778ULL,
        0xffffffffbbbbbbbcULL,
        0x0000000028f5c290ULL,
        0x000000000a3d70a4ULL,
        0x0000000027d27d28ULL,
        0x000000000b60b60cULL,
        0xffffffffcccccccdULL,                    /*  40  */
        0x0000000000000000ULL,
        0xffffffffdddddddeULL,
        0xffffffffeeeeeeefULL,
        0x000000000a3d70a4ULL,
        0xffffffffc28f5c29ULL,
        0x0000000049f49f4aULL,
        0xffffffff82d82d83ULL,
        0x0000000071c71c72ULL,                    /*  48  */
        0x0000000000000000ULL,
        0xffffffffa12f684cULL,
        0xffffffffd097b426ULL,
        0x0000000027d27d28ULL,
        0x0000000049f49f4aULL,
        0xffffffffe06522c4ULL,
        0xffffffff9161f9aeULL,
        0xffffffff8e38e38fULL,                    /*  56  */
        0x0000000000000000ULL,
        0xffffffffb425ed0aULL,
        0xffffffffda12f685ULL,
        0x000000000b60b60cULL,
        0xffffffff82d82d83ULL,
        0xffffffff9161f9aeULL,
        0xfffffffffcd6e9e1ULL,
        0x0000000061639000ULL,                    /*  64  */
        0xffffffffe7176a00ULL,
        0xffffffffc4346000ULL,
        0xffffffffee787980ULL,
        0xffffffffe7176a00ULL,
        0xfffffffffded7040ULL,
        0xffffffffbe6cdc00ULL,
        0x000000002143b470ULL,
        0xffffffffc4346000ULL,                    /*  72  */
        0xffffffffbe6cdc00ULL,
        0xffffffffe5044000ULL,
        0xffffffff89e38100ULL,
        0xffffffffee787980ULL,
        0x000000002143b470ULL,
        0xffffffff89e38100ULL,
        0xffffffffb1e9cfc4ULL,
    };

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_MULU(b64_pattern + i, b64_pattern + j,
                b64_result + (PATTERN_INPUTS_64_SHORT_COUNT * i + j));
        }
    }

    for (i = 0; i < RANDOM_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_MULU(b64_random + i, b64_random + j,
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
