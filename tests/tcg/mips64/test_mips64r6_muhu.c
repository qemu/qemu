/*
 *  Test program for MIPS64R6 instruction MUHU
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
    char *instruction_name =   "MUHU";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b64_result[TEST_COUNT_TOTAL];
    uint64_t b64_expect[TEST_COUNT_TOTAL] = {
        0xfffffffffffffffeULL,                    /*   0  */
        0x0000000000000000ULL,
        0xffffffffaaaaaaa9ULL,
        0x0000000055555554ULL,
        0xffffffffcccccccbULL,
        0x0000000033333332ULL,
        0xffffffffe38e38e2ULL,
        0x000000001c71c71bULL,
        0x0000000000000000ULL,                    /*   8  */
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0xffffffffaaaaaaa9ULL,                    /*  16  */
        0x0000000000000000ULL,
        0x0000000071c71c70ULL,
        0x0000000038e38e38ULL,
        0xffffffff88888887ULL,
        0x0000000022222221ULL,
        0xffffffff97b425ecULL,
        0x0000000012f684bdULL,
        0x0000000055555554ULL,                    /*  24  */
        0x0000000000000000ULL,
        0x0000000038e38e38ULL,
        0x000000001c71c71cULL,
        0x0000000044444443ULL,
        0x0000000011111110ULL,
        0x000000004bda12f6ULL,
        0x00000000097b425eULL,
        0xffffffffcccccccbULL,                    /*  32  */
        0x0000000000000000ULL,
        0xffffffff88888887ULL,
        0x0000000044444443ULL,
        0xffffffffa3d70a3cULL,
        0x0000000028f5c28fULL,
        0xffffffffb60b60b4ULL,
        0x0000000016c16c16ULL,
        0x0000000033333332ULL,                    /*  40  */
        0x0000000000000000ULL,
        0x0000000022222221ULL,
        0x0000000011111110ULL,
        0x0000000028f5c28fULL,
        0x000000000a3d70a3ULL,
        0x000000002d82d82dULL,
        0x0000000005b05b05ULL,
        0xffffffffe38e38e2ULL,                    /*  48  */
        0x0000000000000000ULL,
        0xffffffff97b425ecULL,
        0x000000004bda12f6ULL,
        0xffffffffb60b60b4ULL,
        0x000000002d82d82dULL,
        0xffffffffca4587e5ULL,
        0x000000001948b0fcULL,
        0x000000001c71c71bULL,                    /*  56  */
        0x0000000000000000ULL,
        0x0000000012f684bdULL,
        0x00000000097b425eULL,
        0x0000000016c16c16ULL,
        0x0000000005b05b05ULL,
        0x000000001948b0fcULL,
        0x000000000329161fULL,
        0x0000000048b1c1dcULL,                    /*  64  */
        0xffffffff86260fd6ULL,
        0x000000005bd825b9ULL,
        0x000000003bd8e9d8ULL,
        0xffffffff86260fd6ULL,
        0xfffffffff78e21c6ULL,
        0xffffffffa97cd4d0ULL,
        0x000000006e70e5bbULL,
        0x000000005bd825b9ULL,                    /*  72  */
        0xffffffffa97cd4d0ULL,
        0x000000007409fad3ULL,
        0x000000004b9ceb69ULL,
        0x000000003bd8e9d8ULL,
        0x000000006e70e5bbULL,
        0x000000004b9ceb69ULL,
        0x0000000031454bf2ULL,
    };

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_MUHU(b64_pattern_se + i, b64_pattern_se + j,
                b64_result + (PATTERN_INPUTS_64_SHORT_COUNT * i + j));
        }
    }

    for (i = 0; i < RANDOM_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_MUHU(b64_random_se + i, b64_random_se + j,
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
