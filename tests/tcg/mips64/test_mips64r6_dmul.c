/*
 *  Test program for MIPS64R6 instruction DMUL
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
    char *instruction_name =   "DMUL";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b64_result[TEST_COUNT_TOTAL];
    uint64_t b64_expect[TEST_COUNT_TOTAL] = {
        0x0000000000000001ULL,                    /*   0  */
        0x0000000000000000ULL,
        0x5555555555555556ULL,
        0xaaaaaaaaaaaaaaabULL,
        0x3333333333333334ULL,
        0xcccccccccccccccdULL,
        0x1c71c71c71c71c72ULL,
        0xe38e38e38e38e38fULL,
        0x0000000000000000ULL,                    /*   8  */
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x5555555555555556ULL,                    /*  16  */
        0x0000000000000000ULL,
        0x38e38e38e38e38e4ULL,
        0x1c71c71c71c71c72ULL,
        0x7777777777777778ULL,
        0xdddddddddddddddeULL,
        0x12f684bda12f684cULL,
        0x425ed097b425ed0aULL,
        0xaaaaaaaaaaaaaaabULL,                    /*  24  */
        0x0000000000000000ULL,
        0x1c71c71c71c71c72ULL,
        0x8e38e38e38e38e39ULL,
        0xbbbbbbbbbbbbbbbcULL,
        0xeeeeeeeeeeeeeeefULL,
        0x097b425ed097b426ULL,
        0xa12f684bda12f685ULL,
        0x3333333333333334ULL,                    /*  32  */
        0x0000000000000000ULL,
        0x7777777777777778ULL,
        0xbbbbbbbbbbbbbbbcULL,
        0xf5c28f5c28f5c290ULL,
        0x3d70a3d70a3d70a4ULL,
        0x7d27d27d27d27d28ULL,
        0xb60b60b60b60b60cULL,
        0xcccccccccccccccdULL,                    /*  40  */
        0x0000000000000000ULL,
        0xdddddddddddddddeULL,
        0xeeeeeeeeeeeeeeefULL,
        0x3d70a3d70a3d70a4ULL,
        0x8f5c28f5c28f5c29ULL,
        0x9f49f49f49f49f4aULL,
        0x2d82d82d82d82d83ULL,
        0x1c71c71c71c71c72ULL,                    /*  48  */
        0x0000000000000000ULL,
        0x12f684bda12f684cULL,
        0x097b425ed097b426ULL,
        0x7d27d27d27d27d28ULL,
        0x9f49f49f49f49f4aULL,
        0xb0fcd6e9e06522c4ULL,
        0x6b74f0329161f9aeULL,
        0xe38e38e38e38e38fULL,                    /*  56  */
        0x0000000000000000ULL,
        0x425ed097b425ed0aULL,
        0xa12f684bda12f685ULL,
        0xb60b60b60b60b60cULL,
        0x2d82d82d82d82d83ULL,
        0x6b74f0329161f9aeULL,
        0x781948b0fcd6e9e1ULL,
        0xad45be6961639000ULL,                    /*  64  */
        0xefa7a5a0e7176a00ULL,
        0x08c6139fc4346000ULL,
        0xfbe1883aee787980ULL,
        0xefa7a5a0e7176a00ULL,
        0x37ae2b38fded7040ULL,
        0x6acb3d68be6cdc00ULL,
        0xedbf72842143b470ULL,
        0x08c6139fc4346000ULL,                    /*  72  */
        0x6acb3d68be6cdc00ULL,
        0x8624e5e1e5044000ULL,
        0x76a5ab8089e38100ULL,
        0xfbe1883aee787980ULL,
        0xedbf72842143b470ULL,
        0x76a5ab8089e38100ULL,
        0x4bb436d5b1e9cfc4ULL,
    };

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_DMUL(b64_pattern + i, b64_pattern + j,
                b64_result + (PATTERN_INPUTS_64_SHORT_COUNT * i + j));
        }
    }

    for (i = 0; i < RANDOM_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_DMUL(b64_random + i, b64_random + j,
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
