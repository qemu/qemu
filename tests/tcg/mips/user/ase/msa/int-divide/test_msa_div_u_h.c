/*
 *  Test program for MSA instruction DIV_U.H
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
    char *instruction_name =  "DIV_U.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x0003000300030003ULL, 0x0003000300030003ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x0005000500050005ULL, 0x0005000500050005ULL, },
        { 0x0001000400010001ULL, 0x0004000100010004ULL, },
        { 0x0009000100020009ULL, 0x0001000200090001ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  16  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x0002000200020002ULL, 0x0002000200020002ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0003000300030003ULL, 0x0003000300030003ULL, },
        { 0x0000000300010000ULL, 0x0003000100000003ULL, },
        { 0x0006000000010006ULL, 0x0000000100060000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  24  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x0000000100000000ULL, 0x0001000000000001ULL, },
        { 0x0003000000000003ULL, 0x0000000000030000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  32  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x0002000200020002ULL, 0x0002000200020002ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x0004000400040004ULL, 0x0004000400040004ULL, },
        { 0x0000000300010000ULL, 0x0003000100000003ULL, },
        { 0x0007000100010007ULL, 0x0001000100070001ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  40  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0001000000000001ULL, 0x0000000000010000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  48  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0001000000000001ULL, 0x0000000000010000ULL, },
        { 0x0002000000010002ULL, 0x0000000100020000ULL, },
        { 0x0001000000000001ULL, 0x0000000000010000ULL, },
        { 0x0004000100020004ULL, 0x0001000200040001ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x0008000000010008ULL, 0x0000000100080000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  56  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000100000000ULL, 0x0001000000000001ULL, },
        { 0x0000000200010000ULL, 0x0002000100000002ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000300020000ULL, 0x0003000200000003ULL, },
        { 0x0000000300000000ULL, 0x0003000000000003ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },    /*  64  */
        { 0x0000025400000000ULL, 0x00030000000b0002ULL, },
        { 0x0000000100000000ULL, 0x0001000000010004ULL, },
        { 0x0001000a00000000ULL, 0x0000000000010000ULL, },
        { 0x0001000000010002ULL, 0x0000001000000000ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x0001000000000001ULL, 0x0000000000000002ULL, },
        { 0x0002000000000000ULL, 0x0000000100000000ULL, },
        { 0x0001000000040001ULL, 0x0000001100000000ULL, },    /*  72  */
        { 0x000001c300020000ULL, 0x0002000100080000ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x0001000700010000ULL, 0x0000000100010000ULL, },
        { 0x0000000000020002ULL, 0x0001000c00000001ULL, },
        { 0x0000003900010001ULL, 0x0007000000070002ULL, },
        { 0x0000000000000001ULL, 0x0003000000000006ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_DIV_U_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DIV_U_H(b128_random[i], b128_random[j],
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
