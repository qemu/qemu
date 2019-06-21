/*
 *  Test program for MSA instruction SRLR.W
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
    char *instruction_name =  "SRLR.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000200000002ULL, 0x0000000200000002ULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0040000000400000ULL, 0x0040000000400000ULL, },
        { 0x0000080000000800ULL, 0x0000080000000800ULL, },
        { 0x0010000000100000ULL, 0x0010000000100000ULL, },
        { 0x0000200000002000ULL, 0x0000200000002000ULL, },
        { 0x2000000000040000ULL, 0x0000010020000000ULL, },
        { 0x0000001000008000ULL, 0x0200000000000010ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x002aaaab002aaaabULL, 0x002aaaab002aaaabULL, },
        { 0x0000055500000555ULL, 0x0000055500000555ULL, },
        { 0x000aaaab000aaaabULL, 0x000aaaab000aaaabULL, },
        { 0x0000155500001555ULL, 0x0000155500001555ULL, },
        { 0x155555550002aaabULL, 0x000000ab15555555ULL, },
        { 0x0000000b00005555ULL, 0x015555550000000bULL, },
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x0015555500155555ULL, 0x0015555500155555ULL, },
        { 0x000002ab000002abULL, 0x000002ab000002abULL, },
        { 0x0005555500055555ULL, 0x0005555500055555ULL, },
        { 0x00000aab00000aabULL, 0x00000aab00000aabULL, },
        { 0x0aaaaaab00015555ULL, 0x000000550aaaaaabULL, },
        { 0x0000000500002aabULL, 0x00aaaaab00000005ULL, },
        { 0x0000000200000002ULL, 0x0000000200000002ULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x0033333300333333ULL, 0x0033333300333333ULL, },
        { 0x0000066600000666ULL, 0x0000066600000666ULL, },
        { 0x000ccccd000ccccdULL, 0x000ccccd000ccccdULL, },
        { 0x0000199a0000199aULL, 0x0000199a0000199aULL, },
        { 0x1999999a00033333ULL, 0x000000cd1999999aULL, },
        { 0x0000000d00006666ULL, 0x0199999a0000000dULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x000ccccd000ccccdULL, 0x000ccccd000ccccdULL, },
        { 0x0000019a0000019aULL, 0x0000019a0000019aULL, },
        { 0x0003333300033333ULL, 0x0003333300033333ULL, },
        { 0x0000066600000666ULL, 0x0000066600000666ULL, },
        { 0x066666660000cccdULL, 0x0000003306666666ULL, },
        { 0x000000030000199aULL, 0x0066666600000003ULL, },
        { 0x0000000200000001ULL, 0x0000000000000002ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x0038e38e00238e39ULL, 0x000e38e40038e38eULL, },
        { 0x0000071c00000472ULL, 0x000001c70000071cULL, },
        { 0x000e38e40008e38eULL, 0x00038e39000e38e4ULL, },
        { 0x00001c72000011c7ULL, 0x0000071c00001c72ULL, },
        { 0x1c71c71c000238e4ULL, 0x000000391c71c71cULL, },
        { 0x0000000e0000471cULL, 0x0071c71c0000000eULL, },
        { 0x0000000000000001ULL, 0x0000000200000000ULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x00071c72001c71c7ULL, 0x0031c71c00071c72ULL, },
        { 0x000000e40000038eULL, 0x00000639000000e4ULL, },
        { 0x0001c71c00071c72ULL, 0x000c71c70001c71cULL, },
        { 0x0000038e00000e39ULL, 0x000018e40000038eULL, },
        { 0x038e38e40001c71cULL, 0x000000c7038e38e4ULL, },
        { 0x00000002000038e4ULL, 0x018e38e400000002ULL, },
        { 0x000886ae28625540ULL, 0x00000001000fe7bbULL, },    /*  64  */
        { 0x110d5cda00286255ULL, 0x0000001300000010ULL, },
        { 0x00221aba28625540ULL, 0x0000000100000fe8ULL, },
        { 0x000443570000a189ULL, 0x0000004bfe7bb00cULL, },
        { 0x000fbbe04d93c708ULL, 0x00000000000153f5ULL, },
        { 0x1f77c00c004d93c7ULL, 0x0000000500000001ULL, },
        { 0x003eef804d93c708ULL, 0x0000000000000154ULL, },
        { 0x0007ddf00001364fULL, 0x00000013153f52fcULL, },
        { 0x000ac5abb9cf8b80ULL, 0x00000001000ab2b2ULL, },    /*  72  */
        { 0x158b55d500b9cf8cULL, 0x0000000a0000000bULL, },
        { 0x002b16acb9cf8b80ULL, 0x0000000000000ab3ULL, },
        { 0x000562d50002e73eULL, 0x00000028ab2b2514ULL, },
        { 0x000704f15e31e24eULL, 0x00000002000a942eULL, },
        { 0x0e09e2ca005e31e2ULL, 0x000000230000000bULL, },
        { 0x001c13c65e31e24eULL, 0x0000000100000a94ULL, },
        { 0x00038279000178c8ULL, 0x0000008ea942e2a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRLR_W(b128_pattern[i], b128_pattern[j],
                          b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRLR_W(b128_random[i], b128_random[j],
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
