/*
 *  Test program for MSA instruction SRA.W
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

#include "../../../../include/wrappers_msa.h"
#include "../../../../include/test_inputs_128.h"
#include "../../../../include/test_utils_128.h"

#define TEST_COUNT_TOTAL (                                                \
            (PATTERN_INPUTS_SHORT_COUNT) * (PATTERN_INPUTS_SHORT_COUNT) + \
            (RANDOM_INPUTS_SHORT_COUNT) * (RANDOM_INPUTS_SHORT_COUNT))


int32_t main(void)
{
    char *isa_ase_name = "MSA";
    char *group_name = "Shift";
    char *instruction_name =  "SRA.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xffeaaaaaffeaaaaaULL, 0xffeaaaaaffeaaaaaULL, },
        { 0xfffffd55fffffd55ULL, 0xfffffd55fffffd55ULL, },
        { 0xfffaaaaafffaaaaaULL, 0xfffaaaaafffaaaaaULL, },
        { 0xfffff555fffff555ULL, 0xfffff555fffff555ULL, },
        { 0xf5555555fffeaaaaULL, 0xffffffaaf5555555ULL, },
        { 0xfffffffaffffd555ULL, 0xff555555fffffffaULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x0015555500155555ULL, 0x0015555500155555ULL, },
        { 0x000002aa000002aaULL, 0x000002aa000002aaULL, },
        { 0x0005555500055555ULL, 0x0005555500055555ULL, },
        { 0x00000aaa00000aaaULL, 0x00000aaa00000aaaULL, },
        { 0x0aaaaaaa00015555ULL, 0x000000550aaaaaaaULL, },
        { 0x0000000500002aaaULL, 0x00aaaaaa00000005ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xfff33333fff33333ULL, 0xfff33333fff33333ULL, },
        { 0xfffffe66fffffe66ULL, 0xfffffe66fffffe66ULL, },
        { 0xfffcccccfffcccccULL, 0xfffcccccfffcccccULL, },
        { 0xfffff999fffff999ULL, 0xfffff999fffff999ULL, },
        { 0xf9999999ffff3333ULL, 0xffffffccf9999999ULL, },
        { 0xfffffffcffffe666ULL, 0xff999999fffffffcULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x000ccccc000cccccULL, 0x000ccccc000cccccULL, },
        { 0x0000019900000199ULL, 0x0000019900000199ULL, },
        { 0x0003333300033333ULL, 0x0003333300033333ULL, },
        { 0x0000066600000666ULL, 0x0000066600000666ULL, },
        { 0x066666660000ccccULL, 0x0000003306666666ULL, },
        { 0x0000000300001999ULL, 0x0066666600000003ULL, },
        { 0xffffffffffffffffULL, 0x00000000ffffffffULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xfff8e38effe38e38ULL, 0x000e38e3fff8e38eULL, },
        { 0xffffff1cfffffc71ULL, 0x000001c7ffffff1cULL, },
        { 0xfffe38e3fff8e38eULL, 0x00038e38fffe38e3ULL, },
        { 0xfffffc71fffff1c7ULL, 0x0000071cfffffc71ULL, },
        { 0xfc71c71cfffe38e3ULL, 0x00000038fc71c71cULL, },
        { 0xfffffffeffffc71cULL, 0x0071c71cfffffffeULL, },
        { 0x0000000000000000ULL, 0xffffffff00000000ULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x00071c71001c71c7ULL, 0xfff1c71c00071c71ULL, },
        { 0x000000e30000038eULL, 0xfffffe38000000e3ULL, },
        { 0x0001c71c00071c71ULL, 0xfffc71c70001c71cULL, },
        { 0x0000038e00000e38ULL, 0xfffff8e30000038eULL, },
        { 0x038e38e30001c71cULL, 0xffffffc7038e38e3ULL, },
        { 0x00000001000038e3ULL, 0xff8e38e300000001ULL, },
        { 0xfff886ae28625540ULL, 0x00000001ffffe7bbULL, },    /*  64  */
        { 0xf10d5cd900286255ULL, 0x00000012ffffffffULL, },
        { 0xffe21ab928625540ULL, 0x00000000ffffffe7ULL, },
        { 0xfffc43570000a189ULL, 0x0000004bfe7bb00cULL, },
        { 0xffffbbe04d93c708ULL, 0x00000000000153f5ULL, },
        { 0xff77c00c004d93c7ULL, 0x0000000400000001ULL, },
        { 0xfffeef804d93c708ULL, 0x0000000000000153ULL, },
        { 0xffffddf00001364fULL, 0x00000012153f52fcULL, },
        { 0xfffac5aab9cf8b80ULL, 0x00000000fffab2b2ULL, },    /*  72  */
        { 0xf58b55d5ffb9cf8bULL, 0x00000009fffffffaULL, },
        { 0xffeb16abb9cf8b80ULL, 0x00000000fffffab2ULL, },
        { 0xfffd62d5fffee73eULL, 0x00000027ab2b2514ULL, },
        { 0x000704f15e31e24eULL, 0xfffffffefffa942eULL, },
        { 0x0e09e2c9005e31e2ULL, 0xffffffe3fffffffaULL, },
        { 0x001c13c55e31e24eULL, 0xfffffffffffffa94ULL, },
        { 0x00038278000178c7ULL, 0xffffff8da942e2a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRA_W(b128_pattern[i], b128_pattern[j],
                         b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRA_W(b128_random[i], b128_random[j],
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
