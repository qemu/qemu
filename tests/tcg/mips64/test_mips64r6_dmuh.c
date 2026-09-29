/*
 *  Test program for MIPS64R6 instruction DMUH
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
    char *instruction_name =   "DMUH";
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
        0x1c71c71c71c71c72ULL,
        0xe38e38e38e38e38eULL,
        0x1111111111111111ULL,
        0xeeeeeeeeeeeeeeeeULL,
        0x097b425ed097b426ULL,
        0xf684bda12f684bdaULL,
        0xffffffffffffffffULL,                    /*  24  */
        0x0000000000000000ULL,
        0xe38e38e38e38e38eULL,
        0x1c71c71c71c71c71ULL,
        0xeeeeeeeeeeeeeeeeULL,
        0x1111111111111110ULL,
        0xf684bda12f684bdaULL,
        0x097b425ed097b425ULL,
        0x0000000000000000ULL,                    /*  32  */
        0x0000000000000000ULL,
        0x1111111111111111ULL,
        0xeeeeeeeeeeeeeeeeULL,
        0x0a3d70a3d70a3d70ULL,
        0xf5c28f5c28f5c28fULL,
        0x05b05b05b05b05b0ULL,
        0xfa4fa4fa4fa4fa4fULL,
        0xffffffffffffffffULL,                    /*  40  */
        0x0000000000000000ULL,
        0xeeeeeeeeeeeeeeeeULL,
        0x1111111111111110ULL,
        0xf5c28f5c28f5c28fULL,
        0x0a3d70a3d70a3d70ULL,
        0xfa4fa4fa4fa4fa4fULL,
        0x05b05b05b05b05b0ULL,
        0x0000000000000000ULL,                    /*  48  */
        0x0000000000000000ULL,
        0x097b425ed097b426ULL,
        0xf684bda12f684bdaULL,
        0x05b05b05b05b05b0ULL,
        0xfa4fa4fa4fa4fa4fULL,
        0x0329161f9add3c0cULL,
        0xfcd6e9e06522c3f3ULL,
        0xffffffffffffffffULL,                    /*  56  */
        0x0000000000000000ULL,
        0xf684bda12f684bdaULL,
        0x097b425ed097b425ULL,
        0xfa4fa4fa4fa4fa4fULL,
        0x05b05b05b05b05b0ULL,
        0xfcd6e9e06522c3f3ULL,
        0x0329161f9add3c0cULL,
        0x37dbf4448b48bce3ULL,                    /*  64  */
        0x01fd28a6ebd66e19ULL,
        0x271290430f9643afULL,
        0xcb89d38b96a86603ULL,
        0x01fd28a6ebd66e19ULL,
        0x00122100b25f881aULL,
        0x016425c3dacd63e9ULL,
        0xfe21cf6e9b332df5ULL,
        0x271290430f9643afULL,                    /*  72  */
        0x016425c3dacd63e9ULL,
        0x1b549d7f3d46f8d3ULL,
        0xdb4dd51d1b7c58f2ULL,
        0xcb89d38b96a86603ULL,
        0xfe21cf6e9b332df5ULL,
        0xdb4dd51d1b7c58f2ULL,
        0x31454bf2781d2c60ULL,
    };

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_DMUH(b64_pattern + i, b64_pattern + j,
                b64_result + (PATTERN_INPUTS_64_SHORT_COUNT * i + j));
        }
    }

    for (i = 0; i < RANDOM_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_DMUH(b64_random + i, b64_random + j,
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
