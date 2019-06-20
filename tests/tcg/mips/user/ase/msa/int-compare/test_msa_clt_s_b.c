/*
 *  Test program for MSA instruction CLT_S.B
 *
 *  Copyright (C) 2019  Wave Computing, Inc.
 *  Copyright (C) 2019  Aleksandar Markovic <amarkovic@wavecomp.com>
 *  Copyright (C) 2019  RT-RK Computer Based Systems LLC
 *  Copyright (C) 2019  Mateja Marjanovic <mateja.marjanovic@rt-rk.com>
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

#include "../../../../include/wrappers_msa.h"
#include "../../../../include/test_inputs_128.h"
#include "../../../../include/test_utils_128.h"

#define TEST_COUNT_TOTAL (                                                \
            (PATTERN_INPUTS_SHORT_COUNT) * (PATTERN_INPUTS_SHORT_COUNT) + \
            (RANDOM_INPUTS_SHORT_COUNT) * (RANDOM_INPUTS_SHORT_COUNT))


int32_t main(void)
{
    char *isa_ase_name = "MSA";
    char *group_name = "Int Compare";
    char *instruction_name =  "CLT_S.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000ff0000ff0000ULL, 0xff0000ff0000ff00ULL, },
        { 0xffff00ffff00ffffULL, 0x00ffff00ffff00ffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000ff0000ff0000ULL, 0xff0000ff0000ff00ULL, },
        { 0xffff00ffff00ffffULL, 0x00ffff00ffff00ffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  16  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xff00ffff00ffff00ULL, 0xffff00ffff00ffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  24  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x00ff0000ff0000ffULL, 0x0000ff0000ff0000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  32  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xff00ffff00ffff00ULL, 0xffff00ffff00ffffULL, },
        { 0xffff00ffff00ffffULL, 0x00ffff00ffff00ffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  40  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000ff0000ff0000ULL, 0xff0000ff0000ff00ULL, },
        { 0x00ff0000ff0000ffULL, 0x0000ff0000ff0000ULL, },
        { 0xffff00ffff00ffffULL, 0x00ffff00ffff00ffULL, },    /*  48  */
        { 0xffff00ffff00ffffULL, 0x00ffff00ffff00ffULL, },
        { 0x00ff0000ff0000ffULL, 0x0000ff0000ff0000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x00ff0000ff0000ffULL, 0x0000ff0000ff0000ULL, },
        { 0xffff00ffff00ffffULL, 0x00ffff00ffff00ffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffff00ffff00ffffULL, 0x00ffff00ffff00ffULL, },
        { 0x0000ff0000ff0000ULL, 0xff0000ff0000ff00ULL, },    /*  56  */
        { 0x0000ff0000ff0000ULL, 0xff0000ff0000ff00ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xff00ffff00ffff00ULL, 0xffff00ffff00ffffULL, },
        { 0x0000ff0000ff0000ULL, 0xff0000ff0000ff00ULL, },
        { 0xff00ffff00ffff00ULL, 0xffff00ffff00ffffULL, },
        { 0x0000ff0000ff0000ULL, 0xff0000ff0000ff00ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  64  */
        { 0xff00ffffff000000ULL, 0x00000000ff00ff00ULL, },
        { 0xff00000000000000ULL, 0x000000000000ffffULL, },
        { 0xff00ffffff0000ffULL, 0x000000000000ff00ULL, },
        { 0x00ff000000ffffffULL, 0xffffffff00ff00ffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x00ff000000ff0000ULL, 0xff00ff00000000ffULL, },
        { 0xffffff00ffffffffULL, 0x0000000000ff0000ULL, },
        { 0x00ffffffffffffffULL, 0xffffffffffff0000ULL, },    /*  72  */
        { 0xff00ffffff00ffffULL, 0x00ff00ffffffff00ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xff00ffffffffffffULL, 0x00ff000000ff0000ULL, },
        { 0x00ff000000ffff00ULL, 0xffffffffffff00ffULL, },
        { 0x000000ff00000000ULL, 0xffffffffff00ffffULL, },
        { 0x00ff000000000000ULL, 0xff00ffffff00ffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_CLT_S_B(b128_pattern[i], b128_pattern[j],
                          b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_CLT_S_B(b128_random[i], b128_random[j],
                           b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                        (PATTERN_INPUTS_SHORT_COUNT)) +
                                       RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    gettimeofday(&end, NULL);

    elapsed_time = (end.tv_sec - start.tv_sec) * 1000.0;
    elapsed_time += (end.tv_usec - start.tv_usec) / 1000.0;

    ret = check_results_128(isa_ase_name, group_name, instruction_name,
                            TEST_COUNT_TOTAL, elapsed_time,
                            &b128_result[0][0], &b128_expect[0][0]);

    return ret;
}
