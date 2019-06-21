/*
 *  Test program for MSA instruction DIV_S.W
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
    char *instruction_name =  "DIV_S.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },    /*   0  */
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },
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
        { 0x5555555655555556ULL, 0x5555555655555556ULL, },    /*  16  */
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000200000000ULL, 0xffffffff00000002ULL, },
        { 0xfffffffd00000000ULL, 0x00000001fffffffdULL, },
        { 0xaaaaaaabaaaaaaabULL, 0xaaaaaaabaaaaaaabULL, },    /*  24  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },
        { 0xfffffffe00000000ULL, 0x00000001fffffffeULL, },
        { 0x0000000300000000ULL, 0xffffffff00000003ULL, },
        { 0x3333333433333334ULL, 0x3333333433333334ULL, },    /*  32  */
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000100000000ULL, 0x0000000000000001ULL, },
        { 0xffffffff00000000ULL, 0x00000000ffffffffULL, },
        { 0xcccccccdcccccccdULL, 0xcccccccdcccccccdULL, },    /*  40  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },
        { 0xffffffff00000000ULL, 0x00000000ffffffffULL, },
        { 0x0000000100000000ULL, 0x0000000000000001ULL, },
        { 0x1c71c71d71c71c72ULL, 0xc71c71c81c71c71dULL, },    /*  48  */
        { 0x0000000100000001ULL, 0xffffffff00000001ULL, },
        { 0x0000000000000001ULL, 0x0000000000000000ULL, },
        { 0x00000000ffffffffULL, 0x0000000000000000ULL, },
        { 0x0000000000000002ULL, 0xffffffff00000000ULL, },
        { 0x00000000fffffffeULL, 0x0000000100000000ULL, },
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },
        { 0xffffffffffffffffULL, 0x00000000ffffffffULL, },
        { 0xe38e38e48e38e38fULL, 0x38e38e39e38e38e4ULL, },    /*  56  */
        { 0xffffffffffffffffULL, 0x00000001ffffffffULL, },
        { 0x00000000ffffffffULL, 0x0000000000000000ULL, },
        { 0x0000000000000001ULL, 0x0000000000000000ULL, },
        { 0x00000000fffffffeULL, 0x0000000100000000ULL, },
        { 0x0000000000000002ULL, 0xffffffff00000000ULL, },
        { 0x0000000000000000ULL, 0xffffffff00000000ULL, },
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },    /*  64  */
        { 0x0000001c00000000ULL, 0x0000000300000000ULL, },
        { 0x0000000100000000ULL, 0x0000000100000000ULL, },
        { 0xffffffff00000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000001ULL, 0x00000000fffffff2ULL, },
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },
        { 0x00000000ffffffffULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x00000000ffffffffULL, 0x0000000000000037ULL, },    /*  72  */
        { 0x0000001300000000ULL, 0x00000002fffffffdULL, },
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000002ULL, 0xffffffff00000039ULL, },
        { 0xffffffe600000001ULL, 0xfffffffafffffffcULL, },
        { 0xffffffffffffffffULL, 0xfffffffe00000001ULL, },
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_DIV_S_W(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DIV_S_W(b128_random[i], b128_random[j],
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
