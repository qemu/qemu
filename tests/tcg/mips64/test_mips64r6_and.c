/*
 *  Test program for MIPS64R6 instruction AND
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
    char *group_name = "Logic";
    char *instruction_name =   "AND";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b64_result[TEST_COUNT_TOTAL];
    uint64_t b64_expect[TEST_COUNT_TOTAL] = {
        0x8000000000000000ULL,                    /*   0  */
        0xffffffffffffffffULL,
        0xfffffc0000000000ULL,
        0xffffffffffe00000ULL,
        0xfffffffffffff000ULL,
        0xfff8000000000000ULL,
        0xffffffffffffc000ULL,
        0xfffe000000000000ULL,
        0x0000000000000000ULL,                    /*   8  */
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,                    /*  16  */
        0xaaaaaaaaaaaaaaaaULL,
        0xaaaaa80000000000ULL,
        0x5555555555400000ULL,
        0xaaaaaaaaaaaaa000ULL,
        0x5550000000000000ULL,
        0xaaaaaaaaaaaa8000ULL,
        0x5554000000000000ULL,
        0x8000000000000000ULL,                    /*  24  */
        0x5555555555555555ULL,
        0x5555540000000000ULL,
        0xaaaaaaaaaaa00000ULL,
        0x5555555555555000ULL,
        0xaaa8000000000000ULL,
        0x5555555555554000ULL,
        0xaaaa000000000000ULL,
        0x0000000000000000ULL,                    /*  32  */
        0xccccccccccccccccULL,
        0x3333300000000000ULL,
        0x9999999999800000ULL,
        0xccccccccccccc000ULL,
        0x6660000000000000ULL,
        0x3333333333330000ULL,
        0x9998000000000000ULL,
        0x8000000000000000ULL,                    /*  40  */
        0x3333333333333333ULL,
        0xcccccc0000000000ULL,
        0x6666666666600000ULL,
        0x3333333333333000ULL,
        0x9998000000000000ULL,
        0xccccccccccccc000ULL,
        0x6666000000000000ULL,
        0x0000000000000000ULL,                    /*  48  */
        0xe38e38e38e38e38eULL,
        0xe38e380000000000ULL,
        0x1c71c71c71c00000ULL,
        0xe38e38e38e38e000ULL,
        0x1c70000000000000ULL,
        0x8e38e38e38e38000ULL,
        0xc71c000000000000ULL,
        0x8000000000000000ULL,                    /*  56  */
        0x1c71c71c71c71c71ULL,
        0x1c71c40000000000ULL,
        0xe38e38e38e200000ULL,
        0x1c71c71c71c71000ULL,
        0xe388000000000000ULL,
        0x71c71c71c71c4000ULL,
        0x38e2000000000000ULL,
        0x886ae6cc28625540ULL,                    /*  64  */
        0x6ae6cc2862554000ULL,
        0x886ae6cc28625540ULL,
        0xb9b30a1895500000ULL,
        0xfbbe00634d93c708ULL,
        0xbe00634d93c70800ULL,
        0xfbbe00634d93c708ULL,
        0x8018d364f1c20000ULL,
        0xac5aaeaab9cf8b80ULL,                    /*  72  */
        0x5aaeaab9cf8b8000ULL,
        0xac5aaeaab9cf8b80ULL,
        0xabaaae73e2e00000ULL,
        0x704f164d5e31e24eULL,
        0x4f164d5e31e24e00ULL,
        0x704f164d5e31e24eULL,
        0xc593578c78938000ULL,
    };

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_AND(b64_pattern + i, b64_pattern + j,
                b64_result + (PATTERN_INPUTS_64_SHORT_COUNT * i + j));
        }
    }

    for (i = 0; i < RANDOM_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_AND(b64_random + i, b64_random + j,
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
