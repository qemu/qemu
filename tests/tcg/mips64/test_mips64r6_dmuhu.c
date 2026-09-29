/*
 *  Test program for MIPS64R6 instruction DMUHU
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
    char *instruction_name =   "DMUHU";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b64_result[TEST_COUNT_TOTAL];
    uint64_t b64_expect[TEST_COUNT_TOTAL] = {
        0xfffffffffffffffeULL,                    /*   0  */
        0x0000000000000000ULL,
        0xaaaaaaaaaaaaaaa9ULL,
        0x5555555555555554ULL,
        0xcccccccccccccccbULL,
        0x3333333333333332ULL,
        0xe38e38e38e38e38dULL,
        0x1c71c71c71c71c70ULL,
        0x0000000000000000ULL,                    /*   8  */
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0xaaaaaaaaaaaaaaa9ULL,                    /*  16  */
        0x0000000000000000ULL,
        0x71c71c71c71c71c6ULL,
        0x38e38e38e38e38e3ULL,
        0x8888888888888887ULL,
        0x2222222222222221ULL,
        0x97b425ed097b425eULL,
        0x12f684bda12f684bULL,
        0x5555555555555554ULL,                    /*  24  */
        0x0000000000000000ULL,
        0x38e38e38e38e38e3ULL,
        0x1c71c71c71c71c71ULL,
        0x4444444444444443ULL,
        0x1111111111111110ULL,
        0x4bda12f684bda12fULL,
        0x097b425ed097b425ULL,
        0xcccccccccccccccbULL,                    /*  32  */
        0x0000000000000000ULL,
        0x8888888888888887ULL,
        0x4444444444444443ULL,
        0xa3d70a3d70a3d708ULL,
        0x28f5c28f5c28f5c2ULL,
        0xb60b60b60b60b60aULL,
        0x16c16c16c16c16c0ULL,
        0x3333333333333332ULL,                    /*  40  */
        0x0000000000000000ULL,
        0x2222222222222221ULL,
        0x1111111111111110ULL,
        0x28f5c28f5c28f5c2ULL,
        0x0a3d70a3d70a3d70ULL,
        0x2d82d82d82d82d82ULL,
        0x05b05b05b05b05b0ULL,
        0xe38e38e38e38e38dULL,                    /*  48  */
        0x0000000000000000ULL,
        0x97b425ed097b425eULL,
        0x4bda12f684bda12fULL,
        0xb60b60b60b60b60aULL,
        0x2d82d82d82d82d82ULL,
        0xca4587e6b74f0328ULL,
        0x1948b0fcd6e9e064ULL,
        0x1c71c71c71c71c70ULL,                    /*  56  */
        0x0000000000000000ULL,
        0x12f684bda12f684bULL,
        0x097b425ed097b425ULL,
        0x16c16c16c16c16c0ULL,
        0x05b05b05b05b05b0ULL,
        0x1948b0fcd6e9e064ULL,
        0x0329161f9add3c0cULL,
        0x48b1c1dcdc0d6763ULL,                    /*  64  */
        0x86260fd661cc8a61ULL,
        0x5bd825b9f1c8246fULL,
        0x3bd8e9d8f4da4851ULL,
        0x86260fd661cc8a61ULL,
        0xf78e21c74d87162aULL,
        0xa97cd4d1e230b671ULL,
        0x6e70e5bbf9651043ULL,
        0x5bd825b9f1c8246fULL,                    /*  72  */
        0xa97cd4d1e230b671ULL,
        0x7409fad4b0e60fd3ULL,
        0x4b9ceb6a79ae3b40ULL,
        0x3bd8e9d8f4da4851ULL,
        0x6e70e5bbf9651043ULL,
        0x4b9ceb6a79ae3b40ULL,
        0x31454bf2781d2c60ULL,
    };

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_DMUHU(b64_pattern + i, b64_pattern + j,
                b64_result + (PATTERN_INPUTS_64_SHORT_COUNT * i + j));
        }
    }

    for (i = 0; i < RANDOM_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_DMUHU(b64_random + i, b64_random + j,
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
