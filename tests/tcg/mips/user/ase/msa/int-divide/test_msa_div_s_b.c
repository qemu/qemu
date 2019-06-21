/*
 *  Test program for MSA instruction DIV_S.B
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
    char *instruction_name =  "DIV_S.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },    /*   0  */
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
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
        { 0x5656565656565656ULL, 0x5656565656565656ULL, },    /*  16  */
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0200ff0200ff0200ULL, 0xff0200ff0200ff02ULL, },
        { 0xfd0001fd0001fd00ULL, 0x01fd0001fd0001fdULL, },
        { 0xababababababababULL, 0xababababababababULL, },    /*  24  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0xfe0001fe0001fe00ULL, 0x01fe0001fe0001feULL, },
        { 0x0300ff0300ff0300ULL, 0xff0300ff0300ff03ULL, },
        { 0x3434343434343434ULL, 0x3434343434343434ULL, },    /*  32  */
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0100000100000100ULL, 0x0001000001000001ULL, },
        { 0xff0000ff0000ff00ULL, 0x00ff0000ff0000ffULL, },
        { 0xcdcdcdcdcdcdcdcdULL, 0xcdcdcdcdcdcdcdcdULL, },    /*  40  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0xff0000ff0000ff00ULL, 0x00ff0000ff0000ffULL, },
        { 0x0100000100000100ULL, 0x0001000001000001ULL, },
        { 0x1d72c81d72c81d72ULL, 0xc81d72c81d72c81dULL, },    /*  48  */
        { 0x0101ff0101ff0101ULL, 0xff0101ff0101ff01ULL, },
        { 0x0001000001000001ULL, 0x0000010000010000ULL, },
        { 0x00ff0000ff0000ffULL, 0x0000ff0000ff0000ULL, },
        { 0x0002ff0002ff0002ULL, 0xff0002ff0002ff00ULL, },
        { 0x00fe0100fe0100feULL, 0x0100fe0100fe0100ULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0xffff00ffff00ffffULL, 0x00ffff00ffff00ffULL, },
        { 0xe48f39e48f39e48fULL, 0x39e48f39e48f39e4ULL, },    /*  56  */
        { 0xffff01ffff01ffffULL, 0x01ffff01ffff01ffULL, },
        { 0x00ff0000ff0000ffULL, 0x0000ff0000ff0000ULL, },
        { 0x0001000001000001ULL, 0x0000010000010000ULL, },
        { 0x00fe0100fe0100feULL, 0x0100fe0100fe0100ULL, },
        { 0x0002ff0002ff0002ULL, 0xff0002ff0002ff00ULL, },
        { 0x0000ff0000ff0000ULL, 0xff0000ff0000ff00ULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },    /*  64  */
        { 0x18ff01000000ff08ULL, 0x04f50003000100fdULL, },
        { 0x0101000000fe0000ULL, 0x01fe00a20002fe00ULL, },
        { 0xff01ff000002fe00ULL, 0x00fa00fe00010200ULL, },
        { 0x000000ff01ff0000ULL, 0x0000fa00f600ff00ULL, },
        { 0x0101ff0101010101ULL, 0x0101010101010101ULL, },
        { 0x000000ffff020000ULL, 0x000001e600010200ULL, },
        { 0x0000000100fe0100ULL, 0x000000000000fe00ULL, },
        { 0x00000301ff00fffeULL, 0x0000fb002a000001ULL, },    /*  72  */
        { 0x10ff0100000002f0ULL, 0x02040000fc0000fbULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0x0001fdff00ff03ffULL, 0x000200000000ff00ULL, },
        { 0x000000ff02000001ULL, 0xff00f6002b0000f8ULL, },
        { 0xeaffff0001000009ULL, 0xfa0101fffc010018ULL, },
        { 0xff000000ffff0000ULL, 0xfe000228010100fcULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_DIV_S_B(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DIV_S_B(b128_random[i], b128_random[j],
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
