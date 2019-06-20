/*
 *  Test program for MSA instruction DIV_S.H
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
    char *group_name = "Int Divide";
    char *instruction_name =  "DIV_S.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },    /*   0  */
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5556555655565556ULL, 0x5556555655565556ULL, },    /*  16  */
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0003ffff00000003ULL, 0xffff00000003ffffULL, },
        { 0xfffd00010000fffdULL, 0x00010000fffd0001ULL, },
        { 0xaaabaaabaaabaaabULL, 0xaaabaaabaaabaaabULL, },    /*  24  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0xfffe00010000fffeULL, 0x00010000fffe0001ULL, },
        { 0x0003ffff00000003ULL, 0xffff00000003ffffULL, },
        { 0x3334333433343334ULL, 0x3334333433343334ULL, },    /*  32  */
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0001000000000001ULL, 0x0000000000010000ULL, },
        { 0xffff00000000ffffULL, 0x00000000ffff0000ULL, },
        { 0xcccdcccdcccdcccdULL, 0xcccdcccdcccdcccdULL, },    /*  40  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0xffff00000000ffffULL, 0x00000000ffff0000ULL, },
        { 0x0001000000000001ULL, 0x0000000000010000ULL, },
        { 0x1c72c71d71c81c72ULL, 0xc71d71c81c72c71dULL, },    /*  48  */
        { 0x0001ffff00010001ULL, 0xffff00010001ffffULL, },
        { 0x0000000000010000ULL, 0x0000000100000000ULL, },
        { 0x00000000ffff0000ULL, 0x0000ffff00000000ULL, },
        { 0x0000ffff00020000ULL, 0xffff00020000ffffULL, },
        { 0x00000001fffe0000ULL, 0x0001fffe00000001ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0xffff0000ffffffffULL, 0x0000ffffffff0000ULL, },
        { 0xe38f38e48e39e38fULL, 0x38e48e39e38f38e4ULL, },    /*  56  */
        { 0xffff0001ffffffffULL, 0x0001ffffffff0001ULL, },
        { 0x00000000ffff0000ULL, 0x0000ffff00000000ULL, },
        { 0x0000000000010000ULL, 0x0000000100000000ULL, },
        { 0x00000001fffe0000ULL, 0x0001fffe00000001ULL, },
        { 0x0000ffff00020000ULL, 0xffff00020000ffffULL, },
        { 0x0000ffff00000000ULL, 0xffff00000000ffffULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },    /*  64  */
        { 0x001cffbf0000ffffULL, 0x0003000000000000ULL, },
        { 0x0001000000000000ULL, 0x000100000000fffeULL, },
        { 0xffffffff0000fffeULL, 0x0000000000000002ULL, },
        { 0x0000000000010000ULL, 0x0000fffafff3ffffULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x00000000ffff0000ULL, 0x0000000100000002ULL, },
        { 0x0000000000000001ULL, 0x000000000000fffeULL, },
        { 0x00000003ffffffffULL, 0x0000fffb00370000ULL, },    /*  72  */
        { 0x0013ff2e00000002ULL, 0x00020000fffd0000ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x0000fffd00000003ULL, 0x000000000000ffffULL, },
        { 0x0000000000020000ULL, 0xfffffff600390000ULL, },
        { 0xffe6003900010000ULL, 0xfffa0001fffc0000ULL, },
        { 0xffff0000ffff0000ULL, 0xfffe000200010000ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_DIV_S_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DIV_S_H(b128_random[i], b128_random[j],
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
