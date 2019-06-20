/*
 *  Test program for MSA instruction SRL.W
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
    char *instruction_name =  "SRL.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x003fffff003fffffULL, 0x003fffff003fffffULL, },
        { 0x000007ff000007ffULL, 0x000007ff000007ffULL, },
        { 0x000fffff000fffffULL, 0x000fffff000fffffULL, },
        { 0x00001fff00001fffULL, 0x00001fff00001fffULL, },
        { 0x1fffffff0003ffffULL, 0x000000ff1fffffffULL, },
        { 0x0000000f00007fffULL, 0x01ffffff0000000fULL, },
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
        { 0x002aaaaa002aaaaaULL, 0x002aaaaa002aaaaaULL, },
        { 0x0000055500000555ULL, 0x0000055500000555ULL, },
        { 0x000aaaaa000aaaaaULL, 0x000aaaaa000aaaaaULL, },
        { 0x0000155500001555ULL, 0x0000155500001555ULL, },
        { 0x155555550002aaaaULL, 0x000000aa15555555ULL, },
        { 0x0000000a00005555ULL, 0x015555550000000aULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x0015555500155555ULL, 0x0015555500155555ULL, },
        { 0x000002aa000002aaULL, 0x000002aa000002aaULL, },
        { 0x0005555500055555ULL, 0x0005555500055555ULL, },
        { 0x00000aaa00000aaaULL, 0x00000aaa00000aaaULL, },
        { 0x0aaaaaaa00015555ULL, 0x000000550aaaaaaaULL, },
        { 0x0000000500002aaaULL, 0x00aaaaaa00000005ULL, },
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x0033333300333333ULL, 0x0033333300333333ULL, },
        { 0x0000066600000666ULL, 0x0000066600000666ULL, },
        { 0x000ccccc000cccccULL, 0x000ccccc000cccccULL, },
        { 0x0000199900001999ULL, 0x0000199900001999ULL, },
        { 0x1999999900033333ULL, 0x000000cc19999999ULL, },
        { 0x0000000c00006666ULL, 0x019999990000000cULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x000ccccc000cccccULL, 0x000ccccc000cccccULL, },
        { 0x0000019900000199ULL, 0x0000019900000199ULL, },
        { 0x0003333300033333ULL, 0x0003333300033333ULL, },
        { 0x0000066600000666ULL, 0x0000066600000666ULL, },
        { 0x066666660000ccccULL, 0x0000003306666666ULL, },
        { 0x0000000300001999ULL, 0x0066666600000003ULL, },
        { 0x0000000100000001ULL, 0x0000000000000001ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x0038e38e00238e38ULL, 0x000e38e30038e38eULL, },
        { 0x0000071c00000471ULL, 0x000001c70000071cULL, },
        { 0x000e38e30008e38eULL, 0x00038e38000e38e3ULL, },
        { 0x00001c71000011c7ULL, 0x0000071c00001c71ULL, },
        { 0x1c71c71c000238e3ULL, 0x000000381c71c71cULL, },
        { 0x0000000e0000471cULL, 0x0071c71c0000000eULL, },
        { 0x0000000000000000ULL, 0x0000000100000000ULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x00071c71001c71c7ULL, 0x0031c71c00071c71ULL, },
        { 0x000000e30000038eULL, 0x00000638000000e3ULL, },
        { 0x0001c71c00071c71ULL, 0x000c71c70001c71cULL, },
        { 0x0000038e00000e38ULL, 0x000018e30000038eULL, },
        { 0x038e38e30001c71cULL, 0x000000c7038e38e3ULL, },
        { 0x00000001000038e3ULL, 0x018e38e300000001ULL, },
        { 0x000886ae28625540ULL, 0x00000001000fe7bbULL, },    /*  64  */
        { 0x110d5cd900286255ULL, 0x000000120000000fULL, },
        { 0x00221ab928625540ULL, 0x0000000000000fe7ULL, },
        { 0x000443570000a189ULL, 0x0000004bfe7bb00cULL, },
        { 0x000fbbe04d93c708ULL, 0x00000000000153f5ULL, },
        { 0x1f77c00c004d93c7ULL, 0x0000000400000001ULL, },
        { 0x003eef804d93c708ULL, 0x0000000000000153ULL, },
        { 0x0007ddf00001364fULL, 0x00000012153f52fcULL, },
        { 0x000ac5aab9cf8b80ULL, 0x00000000000ab2b2ULL, },    /*  72  */
        { 0x158b55d500b9cf8bULL, 0x000000090000000aULL, },
        { 0x002b16abb9cf8b80ULL, 0x0000000000000ab2ULL, },
        { 0x000562d50002e73eULL, 0x00000027ab2b2514ULL, },
        { 0x000704f15e31e24eULL, 0x00000002000a942eULL, },
        { 0x0e09e2c9005e31e2ULL, 0x000000230000000aULL, },
        { 0x001c13c55e31e24eULL, 0x0000000100000a94ULL, },
        { 0x00038278000178c7ULL, 0x0000008da942e2a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRL_W(b128_pattern[i], b128_pattern[j],
                         b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRL_W(b128_random[i], b128_random[j],
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
