/*
 *  Test program for MSA instruction HSUB_S.W
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
    char *group_name = "Int Subtract";
    char *instruction_name =  "HSUB_S.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000555500005555ULL, 0x0000555500005555ULL, },
        { 0xffffaaaaffffaaaaULL, 0xffffaaaaffffaaaaULL, },
        { 0x0000333300003333ULL, 0x0000333300003333ULL, },
        { 0xffffccccffffccccULL, 0xffffccccffffccccULL, },
        { 0xffffc71c00001c71ULL, 0x000071c7ffffc71cULL, },
        { 0x000038e3ffffe38eULL, 0xffff8e38000038e3ULL, },
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000555600005556ULL, 0x0000555600005556ULL, },
        { 0xffffaaabffffaaabULL, 0xffffaaabffffaaabULL, },
        { 0x0000333400003334ULL, 0x0000333400003334ULL, },
        { 0xffffcccdffffcccdULL, 0xffffcccdffffcccdULL, },
        { 0xffffc71d00001c72ULL, 0x000071c8ffffc71dULL, },
        { 0x000038e4ffffe38fULL, 0xffff8e39000038e4ULL, },
        { 0xffffaaabffffaaabULL, 0xffffaaabffffaaabULL, },    /*  16  */
        { 0xffffaaaaffffaaaaULL, 0xffffaaaaffffaaaaULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffff5555ffff5555ULL, 0xffff5555ffff5555ULL, },
        { 0xffffdddeffffdddeULL, 0xffffdddeffffdddeULL, },
        { 0xffff7777ffff7777ULL, 0xffff7777ffff7777ULL, },
        { 0xffff71c7ffffc71cULL, 0x00001c72ffff71c7ULL, },
        { 0xffffe38effff8e39ULL, 0xffff38e3ffffe38eULL, },
        { 0x0000555600005556ULL, 0x0000555600005556ULL, },    /*  24  */
        { 0x0000555500005555ULL, 0x0000555500005555ULL, },
        { 0x0000aaab0000aaabULL, 0x0000aaab0000aaabULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000888900008889ULL, 0x0000888900008889ULL, },
        { 0x0000222200002222ULL, 0x0000222200002222ULL, },
        { 0x00001c72000071c7ULL, 0x0000c71d00001c72ULL, },
        { 0x00008e39000038e4ULL, 0xffffe38e00008e39ULL, },
        { 0xffffcccdffffcccdULL, 0xffffcccdffffcccdULL, },    /*  32  */
        { 0xffffccccffffccccULL, 0xffffccccffffccccULL, },
        { 0x0000222200002222ULL, 0x0000222200002222ULL, },
        { 0xffff7777ffff7777ULL, 0xffff7777ffff7777ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffff9999ffff9999ULL, 0xffff9999ffff9999ULL, },
        { 0xffff93e9ffffe93eULL, 0x00003e94ffff93e9ULL, },
        { 0x000005b0ffffb05bULL, 0xffff5b05000005b0ULL, },
        { 0x0000333400003334ULL, 0x0000333400003334ULL, },    /*  40  */
        { 0x0000333300003333ULL, 0x0000333300003333ULL, },
        { 0x0000888900008889ULL, 0x0000888900008889ULL, },
        { 0xffffdddeffffdddeULL, 0xffffdddeffffdddeULL, },
        { 0x0000666700006667ULL, 0x0000666700006667ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xfffffa5000004fa5ULL, 0x0000a4fbfffffa50ULL, },
        { 0x00006c17000016c2ULL, 0xffffc16c00006c17ULL, },
        { 0xffffe38fffff8e39ULL, 0x000038e4ffffe38fULL, },    /*  48  */
        { 0xffffe38effff8e38ULL, 0x000038e3ffffe38eULL, },
        { 0x000038e4ffffe38eULL, 0x00008e39000038e4ULL, },
        { 0xffff8e39ffff38e3ULL, 0xffffe38effff8e39ULL, },
        { 0x000016c2ffffc16cULL, 0x00006c17000016c2ULL, },
        { 0xffffb05bffff5b05ULL, 0x000005b0ffffb05bULL, },
        { 0xffffaaabffffaaaaULL, 0x0000aaabffffaaabULL, },
        { 0x00001c72ffff71c7ULL, 0xffffc71c00001c72ULL, },
        { 0x00001c72000071c8ULL, 0xffffc71d00001c72ULL, },    /*  56  */
        { 0x00001c71000071c7ULL, 0xffffc71c00001c71ULL, },
        { 0x000071c70000c71dULL, 0x00001c72000071c7ULL, },
        { 0xffffc71c00001c72ULL, 0xffff71c7ffffc71cULL, },
        { 0x00004fa50000a4fbULL, 0xfffffa5000004fa5ULL, },
        { 0xffffe93e00003e94ULL, 0xffff93e9ffffe93eULL, },
        { 0xffffe38e00008e39ULL, 0x000038e4ffffe38eULL, },
        { 0x0000555500005556ULL, 0xffff555500005555ULL, },
        { 0xffffa19effffd322ULL, 0x0000400900004e6fULL, },    /*  64  */
        { 0xffff88070000615aULL, 0x0000904dffffab7fULL, },
        { 0xffffd9c000009ce2ULL, 0x00008468ffffd967ULL, },
        { 0xffff721d00004614ULL, 0x0000c28f00001bdbULL, },
        { 0x000014f2fffff853ULL, 0x0000079900006533ULL, },
        { 0xfffffb5b0000868bULL, 0x000057ddffffc243ULL, },
        { 0x00004d140000c213ULL, 0x00004bf8fffff02bULL, },
        { 0xffffe57100006b45ULL, 0x00008a1f0000329fULL, },
        { 0xffffc58effff648fULL, 0x00001c7afffffb1fULL, },    /*  72  */
        { 0xffffabf7fffff2c7ULL, 0x00006cbeffff582fULL, },
        { 0xfffffdb000002e4fULL, 0x000060d9ffff8617ULL, },
        { 0xffff960dffffd781ULL, 0x00009f00ffffc88bULL, },
        { 0x00008983000008f1ULL, 0xffff8293fffff936ULL, },
        { 0x00006fec00009729ULL, 0xffffd2d7ffff5646ULL, },
        { 0x0000c1a50000d2b1ULL, 0xffffc6f2ffff842eULL, },
        { 0x00005a0200007be3ULL, 0x00000519ffffc6a2ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_HSUB_S_W(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_HSUB_S_W(b128_random[i], b128_random[j],
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
