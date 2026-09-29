/*
 *  Test program for MIPS64R6 instruction MUH
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
    char *instruction_name =   "MUH";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b64_result[TEST_COUNT_TOTAL];
    uint64_t b64_expect[TEST_COUNT_TOTAL] = {
        0x0000000000000000ULL,                    /*   0  */
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0xffffffffffffffffULL,
        0x0000000000000000ULL,
        0xffffffffffffffffULL,
        0x0000000000000000ULL,
        0xffffffffffffffffULL,
        0x0000000000000000ULL,                    /*   8  */
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,                    /*  16  */
        0x0000000000000000ULL,
        0x000000001c71c71cULL,
        0xffffffffe38e38e3ULL,
        0x0000000011111111ULL,
        0xffffffffeeeeeeeeULL,
        0x00000000097b425fULL,
        0xfffffffff684bda1ULL,
        0xffffffffffffffffULL,                    /*  24  */
        0x0000000000000000ULL,
        0xffffffffe38e38e3ULL,
        0x000000001c71c71cULL,
        0xffffffffeeeeeeeeULL,
        0x0000000011111110ULL,
        0xfffffffff684bda1ULL,
        0x00000000097b425eULL,
        0x0000000000000000ULL,                    /*  32  */
        0x0000000000000000ULL,
        0x0000000011111111ULL,
        0xffffffffeeeeeeeeULL,
        0x000000000a3d70a4ULL,
        0xfffffffff5c28f5cULL,
        0x0000000005b05b05ULL,
        0xfffffffffa4fa4faULL,
        0xffffffffffffffffULL,                    /*  40  */
        0x0000000000000000ULL,
        0xffffffffeeeeeeeeULL,
        0x0000000011111110ULL,
        0xfffffffff5c28f5cULL,
        0x000000000a3d70a3ULL,
        0xfffffffffa4fa4faULL,
        0x0000000005b05b05ULL,
        0x0000000000000000ULL,                    /*  48  */
        0x0000000000000000ULL,
        0x00000000097b425fULL,
        0xfffffffff684bda1ULL,
        0x0000000005b05b05ULL,
        0xfffffffffa4fa4faULL,
        0x000000000329161fULL,
        0xfffffffffcd6e9e0ULL,
        0xffffffffffffffffULL,                    /*  56  */
        0x0000000000000000ULL,
        0xfffffffff684bda1ULL,
        0x00000000097b425eULL,
        0xfffffffffa4fa4faULL,
        0x0000000005b05b05ULL,
        0xfffffffffcd6e9e0ULL,
        0x000000000329161fULL,
        0x0000000037dbf444ULL,                    /*  64  */
        0x0000000001fd28a7ULL,
        0x0000000027129043ULL,
        0xffffffffcb89d38bULL,
        0x0000000001fd28a7ULL,
        0x0000000000122100ULL,
        0x00000000016425c3ULL,
        0xfffffffffe21cf6eULL,
        0x0000000027129043ULL,                    /*  72  */
        0x00000000016425c3ULL,
        0x000000001b549d7fULL,
        0xffffffffdb4dd51cULL,
        0xffffffffcb89d38bULL,
        0xfffffffffe21cf6eULL,
        0xffffffffdb4dd51cULL,
        0x0000000031454bf2ULL,
    };

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_MUH(b64_pattern_se + i, b64_pattern_se + j,
                b64_result + (PATTERN_INPUTS_64_SHORT_COUNT * i + j));
        }
    }

    for (i = 0; i < RANDOM_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_MUH(b64_random_se + i, b64_random_se + j,
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
