/*
 *  Test program for MIPS64R6 instruction SLLV
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
    char *instruction_name =   "SLLV";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b64_result[TEST_COUNT_TOTAL];
    uint64_t b64_expect[TEST_COUNT_TOTAL] = {
        0xffffffff80000000ULL,                    /*   0  */
        0xffffffffffffffffULL,
        0xfffffffffffffc00ULL,
        0xffffffffffe00000ULL,
        0xfffffffffffff000ULL,
        0xfffffffffff80000ULL,
        0xffffffffffffc000ULL,
        0xfffffffffffe0000ULL,
        0x0000000000000000ULL,                    /*   8  */
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,
        0x0000000000000000ULL,                    /*  16  */
        0xffffffffaaaaaaaaULL,
        0xffffffffaaaaa800ULL,
        0x0000000055400000ULL,
        0xffffffffaaaaa000ULL,
        0x0000000055500000ULL,
        0xffffffffaaaa8000ULL,
        0x0000000055540000ULL,
        0xffffffff80000000ULL,                    /*  24  */
        0x0000000055555555ULL,
        0x0000000055555400ULL,
        0xffffffffaaa00000ULL,
        0x0000000055555000ULL,
        0xffffffffaaa80000ULL,
        0x0000000055554000ULL,
        0xffffffffaaaa0000ULL,
        0x0000000000000000ULL,                    /*  32  */
        0xffffffffccccccccULL,
        0x0000000033333000ULL,
        0xffffffff99800000ULL,
        0xffffffffccccc000ULL,
        0x0000000066600000ULL,
        0x0000000033330000ULL,
        0xffffffff99980000ULL,
        0xffffffff80000000ULL,                    /*  40  */
        0x0000000033333333ULL,
        0xffffffffcccccc00ULL,
        0x0000000066600000ULL,
        0x0000000033333000ULL,
        0xffffffff99980000ULL,
        0xffffffffccccc000ULL,
        0x0000000066660000ULL,
        0x0000000000000000ULL,                    /*  48  */
        0xffffffff8e38e38eULL,
        0xffffffffe38e3800ULL,
        0x0000000071c00000ULL,
        0xffffffff8e38e000ULL,
        0x000000001c700000ULL,
        0x0000000038e38000ULL,
        0xffffffffc71c0000ULL,
        0xffffffff80000000ULL,                    /*  56  */
        0x0000000071c71c71ULL,
        0x000000001c71c400ULL,
        0xffffffff8e200000ULL,
        0x0000000071c71000ULL,
        0xffffffffe3880000ULL,
        0xffffffffc71c4000ULL,
        0x0000000038e20000ULL,
        0x0000000028625540ULL,                    /*  64  */
        0x0000000062554000ULL,
        0x0000000028625540ULL,
        0xffffffff95500000ULL,
        0x000000004d93c708ULL,
        0xffffffff93c70800ULL,
        0x000000004d93c708ULL,
        0xfffffffff1c20000ULL,
        0xffffffffb9cf8b80ULL,                    /*  72  */
        0xffffffffcf8b8000ULL,
        0xffffffffb9cf8b80ULL,
        0xffffffffe2e00000ULL,
        0x000000005e31e24eULL,
        0x0000000031e24e00ULL,
        0x000000005e31e24eULL,
        0x0000000078938000ULL,
    };

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_SLLV(b64_pattern + i, b64_pattern + j,
                b64_result + (PATTERN_INPUTS_64_SHORT_COUNT * i + j));
        }
    }

    for (i = 0; i < RANDOM_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_SLLV(b64_random + i, b64_random + j,
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
