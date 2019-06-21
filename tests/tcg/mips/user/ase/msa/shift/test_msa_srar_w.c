/*
 *  Test program for MSA instruction SRAR.W
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
    char *instruction_name =  "SRAR.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
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
        { 0xffeaaaabffeaaaabULL, 0xffeaaaabffeaaaabULL, },
        { 0xfffffd55fffffd55ULL, 0xfffffd55fffffd55ULL, },
        { 0xfffaaaabfffaaaabULL, 0xfffaaaabfffaaaabULL, },
        { 0xfffff555fffff555ULL, 0xfffff555fffff555ULL, },
        { 0xf5555555fffeaaabULL, 0xffffffabf5555555ULL, },
        { 0xfffffffbffffd555ULL, 0xff555555fffffffbULL, },
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x0015555500155555ULL, 0x0015555500155555ULL, },
        { 0x000002ab000002abULL, 0x000002ab000002abULL, },
        { 0x0005555500055555ULL, 0x0005555500055555ULL, },
        { 0x00000aab00000aabULL, 0x00000aab00000aabULL, },
        { 0x0aaaaaab00015555ULL, 0x000000550aaaaaabULL, },
        { 0x0000000500002aabULL, 0x00aaaaab00000005ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xfff33333fff33333ULL, 0xfff33333fff33333ULL, },
        { 0xfffffe66fffffe66ULL, 0xfffffe66fffffe66ULL, },
        { 0xfffccccdfffccccdULL, 0xfffccccdfffccccdULL, },
        { 0xfffff99afffff99aULL, 0xfffff99afffff99aULL, },
        { 0xf999999affff3333ULL, 0xffffffcdf999999aULL, },
        { 0xfffffffdffffe666ULL, 0xff99999afffffffdULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x000ccccd000ccccdULL, 0x000ccccd000ccccdULL, },
        { 0x0000019a0000019aULL, 0x0000019a0000019aULL, },
        { 0x0003333300033333ULL, 0x0003333300033333ULL, },
        { 0x0000066600000666ULL, 0x0000066600000666ULL, },
        { 0x066666660000cccdULL, 0x0000003306666666ULL, },
        { 0x000000030000199aULL, 0x0066666600000003ULL, },
        { 0x00000000ffffffffULL, 0x0000000000000000ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xfff8e38effe38e39ULL, 0x000e38e4fff8e38eULL, },
        { 0xffffff1cfffffc72ULL, 0x000001c7ffffff1cULL, },
        { 0xfffe38e4fff8e38eULL, 0x00038e39fffe38e4ULL, },
        { 0xfffffc72fffff1c7ULL, 0x0000071cfffffc72ULL, },
        { 0xfc71c71cfffe38e4ULL, 0x00000039fc71c71cULL, },
        { 0xfffffffeffffc71cULL, 0x0071c71cfffffffeULL, },
        { 0x0000000000000001ULL, 0x0000000000000000ULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x00071c72001c71c7ULL, 0xfff1c71c00071c72ULL, },
        { 0x000000e40000038eULL, 0xfffffe39000000e4ULL, },
        { 0x0001c71c00071c72ULL, 0xfffc71c70001c71cULL, },
        { 0x0000038e00000e39ULL, 0xfffff8e40000038eULL, },
        { 0x038e38e40001c71cULL, 0xffffffc7038e38e4ULL, },
        { 0x00000002000038e4ULL, 0xff8e38e400000002ULL, },
        { 0xfff886ae28625540ULL, 0x00000001ffffe7bbULL, },    /*  64  */
        { 0xf10d5cda00286255ULL, 0x0000001300000000ULL, },
        { 0xffe21aba28625540ULL, 0x00000001ffffffe8ULL, },
        { 0xfffc43570000a189ULL, 0x0000004bfe7bb00cULL, },
        { 0xffffbbe04d93c708ULL, 0x00000000000153f5ULL, },
        { 0xff77c00c004d93c7ULL, 0x0000000500000001ULL, },
        { 0xfffeef804d93c708ULL, 0x0000000000000154ULL, },
        { 0xffffddf00001364fULL, 0x00000013153f52fcULL, },
        { 0xfffac5abb9cf8b80ULL, 0x00000001fffab2b2ULL, },    /*  72  */
        { 0xf58b55d5ffb9cf8cULL, 0x0000000afffffffbULL, },
        { 0xffeb16acb9cf8b80ULL, 0x00000000fffffab3ULL, },
        { 0xfffd62d5fffee73eULL, 0x00000028ab2b2514ULL, },
        { 0x000704f15e31e24eULL, 0xfffffffefffa942eULL, },
        { 0x0e09e2ca005e31e2ULL, 0xffffffe3fffffffbULL, },
        { 0x001c13c65e31e24eULL, 0xfffffffffffffa94ULL, },
        { 0x00038279000178c8ULL, 0xffffff8ea942e2a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRAR_W(b128_pattern[i], b128_pattern[j],
                          b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRAR_W(b128_random[i], b128_random[j],
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
