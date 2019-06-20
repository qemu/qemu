/*
 *  Test program for MSA instruction HSUB_U.W
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
    char *instruction_name =  "HSUB_U.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0x0000ffff0000ffffULL, 0x0000ffff0000ffffULL, },
        { 0x0000555500005555ULL, 0x0000555500005555ULL, },
        { 0x0000aaaa0000aaaaULL, 0x0000aaaa0000aaaaULL, },
        { 0x0000333300003333ULL, 0x0000333300003333ULL, },
        { 0x0000cccc0000ccccULL, 0x0000cccc0000ccccULL, },
        { 0x0000c71c00001c71ULL, 0x000071c70000c71cULL, },
        { 0x000038e30000e38eULL, 0x00008e38000038e3ULL, },
        { 0xffff0001ffff0001ULL, 0xffff0001ffff0001ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffff5556ffff5556ULL, 0xffff5556ffff5556ULL, },
        { 0xffffaaabffffaaabULL, 0xffffaaabffffaaabULL, },
        { 0xffff3334ffff3334ULL, 0xffff3334ffff3334ULL, },
        { 0xffffcccdffffcccdULL, 0xffffcccdffffcccdULL, },
        { 0xffffc71dffff1c72ULL, 0xffff71c8ffffc71dULL, },
        { 0xffff38e4ffffe38fULL, 0xffff8e39ffff38e4ULL, },
        { 0xffffaaabffffaaabULL, 0xffffaaabffffaaabULL, },    /*  16  */
        { 0x0000aaaa0000aaaaULL, 0x0000aaaa0000aaaaULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000555500005555ULL, 0x0000555500005555ULL, },
        { 0xffffdddeffffdddeULL, 0xffffdddeffffdddeULL, },
        { 0x0000777700007777ULL, 0x0000777700007777ULL, },
        { 0x000071c7ffffc71cULL, 0x00001c72000071c7ULL, },
        { 0xffffe38e00008e39ULL, 0x000038e3ffffe38eULL, },
        { 0xffff5556ffff5556ULL, 0xffff5556ffff5556ULL, },    /*  24  */
        { 0x0000555500005555ULL, 0x0000555500005555ULL, },
        { 0xffffaaabffffaaabULL, 0xffffaaabffffaaabULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffff8889ffff8889ULL, 0xffff8889ffff8889ULL, },
        { 0x0000222200002222ULL, 0x0000222200002222ULL, },
        { 0x00001c72ffff71c7ULL, 0xffffc71d00001c72ULL, },
        { 0xffff8e39000038e4ULL, 0xffffe38effff8e39ULL, },
        { 0xffffcccdffffcccdULL, 0xffffcccdffffcccdULL, },    /*  32  */
        { 0x0000cccc0000ccccULL, 0x0000cccc0000ccccULL, },
        { 0x0000222200002222ULL, 0x0000222200002222ULL, },
        { 0x0000777700007777ULL, 0x0000777700007777ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000999900009999ULL, 0x0000999900009999ULL, },
        { 0x000093e9ffffe93eULL, 0x00003e94000093e9ULL, },
        { 0x000005b00000b05bULL, 0x00005b05000005b0ULL, },
        { 0xffff3334ffff3334ULL, 0xffff3334ffff3334ULL, },    /*  40  */
        { 0x0000333300003333ULL, 0x0000333300003333ULL, },
        { 0xffff8889ffff8889ULL, 0xffff8889ffff8889ULL, },
        { 0xffffdddeffffdddeULL, 0xffffdddeffffdddeULL, },
        { 0xffff6667ffff6667ULL, 0xffff6667ffff6667ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xfffffa50ffff4fa5ULL, 0xffffa4fbfffffa50ULL, },
        { 0xffff6c17000016c2ULL, 0xffffc16cffff6c17ULL, },
        { 0xffffe38fffff8e39ULL, 0xffff38e4ffffe38fULL, },    /*  48  */
        { 0x0000e38e00008e38ULL, 0x000038e30000e38eULL, },
        { 0x000038e4ffffe38eULL, 0xffff8e39000038e4ULL, },
        { 0x00008e39000038e3ULL, 0xffffe38e00008e39ULL, },
        { 0x000016c2ffffc16cULL, 0xffff6c17000016c2ULL, },
        { 0x0000b05b00005b05ULL, 0x000005b00000b05bULL, },
        { 0x0000aaabffffaaaaULL, 0xffffaaab0000aaabULL, },
        { 0x00001c72000071c7ULL, 0xffffc71c00001c72ULL, },
        { 0xffff1c72ffff71c8ULL, 0xffffc71dffff1c72ULL, },    /*  56  */
        { 0x00001c71000071c7ULL, 0x0000c71c00001c71ULL, },
        { 0xffff71c7ffffc71dULL, 0x00001c72ffff71c7ULL, },
        { 0xffffc71c00001c72ULL, 0x000071c7ffffc71cULL, },
        { 0xffff4fa5ffffa4fbULL, 0xfffffa50ffff4fa5ULL, },
        { 0xffffe93e00003e94ULL, 0x000093e9ffffe93eULL, },
        { 0xffffe38effff8e39ULL, 0x000038e4ffffe38eULL, },
        { 0xffff555500005556ULL, 0x00005555ffff5555ULL, },
        { 0xffffa19effffd322ULL, 0x0000400900004e6fULL, },    /*  64  */
        { 0x00008807ffff615aULL, 0xffff904d0000ab7fULL, },
        { 0xffffd9c0ffff9ce2ULL, 0xffff84680000d967ULL, },
        { 0x0000721dffff4614ULL, 0xffffc28f00001bdbULL, },
        { 0x000014f2fffff853ULL, 0x00000799ffff6533ULL, },
        { 0x0000fb5bffff868bULL, 0xffff57ddffffc243ULL, },
        { 0x00004d14ffffc213ULL, 0xffff4bf8fffff02bULL, },
        { 0x0000e571ffff6b45ULL, 0xffff8a1fffff329fULL, },
        { 0xffffc58e0000648fULL, 0x00001c7afffffb1fULL, },    /*  72  */
        { 0x0000abf7fffff2c7ULL, 0xffff6cbe0000582fULL, },
        { 0xfffffdb000002e4fULL, 0xffff60d900008617ULL, },
        { 0x0000960dffffd781ULL, 0xffff9f00ffffc88bULL, },
        { 0xffff8983000008f1ULL, 0x00008293fffff936ULL, },
        { 0x00006fecffff9729ULL, 0xffffd2d700005646ULL, },
        { 0xffffc1a5ffffd2b1ULL, 0xffffc6f20000842eULL, },
        { 0x00005a02ffff7be3ULL, 0x00000519ffffc6a2ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_HSUB_U_W(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_HSUB_U_W(b128_random[i], b128_random[j],
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
