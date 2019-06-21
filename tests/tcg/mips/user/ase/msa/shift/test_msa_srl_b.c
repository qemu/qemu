/*
 *  Test program for MSA instruction SRL.B
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
    char *instruction_name =  "SRL.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x3f3f3f3f3f3f3f3fULL, 0x3f3f3f3f3f3f3f3fULL, },
        { 0x0707070707070707ULL, 0x0707070707070707ULL, },
        { 0x0f0f0f0f0f0f0f0fULL, 0x0f0f0f0f0f0f0f0fULL, },
        { 0x1f1f1f1f1f1f1f1fULL, 0x1f1f1f1f1f1f1f1fULL, },
        { 0x1f03ff1f03ff1f03ULL, 0xff1f03ff1f03ff1fULL, },
        { 0x0f7f010f7f010f7fULL, 0x010f7f010f7f010fULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x2a2a2a2a2a2a2a2aULL, 0x2a2a2a2a2a2a2a2aULL, },
        { 0x0505050505050505ULL, 0x0505050505050505ULL, },
        { 0x0a0a0a0a0a0a0a0aULL, 0x0a0a0a0a0a0a0a0aULL, },
        { 0x1515151515151515ULL, 0x1515151515151515ULL, },
        { 0x1502aa1502aa1502ULL, 0xaa1502aa1502aa15ULL, },
        { 0x0a55010a55010a55ULL, 0x010a55010a55010aULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x1515151515151515ULL, 0x1515151515151515ULL, },
        { 0x0202020202020202ULL, 0x0202020202020202ULL, },
        { 0x0505050505050505ULL, 0x0505050505050505ULL, },
        { 0x0a0a0a0a0a0a0a0aULL, 0x0a0a0a0a0a0a0a0aULL, },
        { 0x0a01550a01550a01ULL, 0x550a01550a01550aULL, },
        { 0x052a00052a00052aULL, 0x00052a00052a0005ULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x0606060606060606ULL, 0x0606060606060606ULL, },
        { 0x0c0c0c0c0c0c0c0cULL, 0x0c0c0c0c0c0c0c0cULL, },
        { 0x1919191919191919ULL, 0x1919191919191919ULL, },
        { 0x1903cc1903cc1903ULL, 0xcc1903cc1903cc19ULL, },
        { 0x0c66010c66010c66ULL, 0x010c66010c66010cULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x0c0c0c0c0c0c0c0cULL, 0x0c0c0c0c0c0c0c0cULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0x0303030303030303ULL, 0x0303030303030303ULL, },
        { 0x0606060606060606ULL, 0x0606060606060606ULL, },
        { 0x0600330600330600ULL, 0x3306003306003306ULL, },
        { 0x0319000319000319ULL, 0x0003190003190003ULL, },
        { 0x0101000101000101ULL, 0x0001010001010001ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x38230e38230e3823ULL, 0x0e38230e38230e38ULL, },
        { 0x0704010704010704ULL, 0x0107040107040107ULL, },
        { 0x0e08030e08030e08ULL, 0x030e08030e08030eULL, },
        { 0x1c11071c11071c11ULL, 0x071c11071c11071cULL, },
        { 0x1c02381c02381c02ULL, 0x381c02381c02381cULL, },
        { 0x0e47000e47000e47ULL, 0x000e47000e47000eULL, },
        { 0x0000010000010000ULL, 0x0100000100000100ULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x071c31071c31071cULL, 0x31071c31071c3107ULL, },
        { 0x0003060003060003ULL, 0x0600030600030600ULL, },
        { 0x01070c01070c0107ULL, 0x0c01070c01070c01ULL, },
        { 0x030e18030e18030eULL, 0x18030e18030e1803ULL, },
        { 0x0301c70301c70301ULL, 0xc70301c70301c703ULL, },
        { 0x0138010138010138ULL, 0x0101380101380101ULL, },
        { 0x881a030c28180240ULL, 0x09000101030fb000ULL, },    /*  64  */
        { 0x1101e619010c0040ULL, 0x1200011707002c00ULL, },
        { 0x081a033314000a40ULL, 0x006700001f0f0500ULL, },
        { 0x8800030600311501ULL, 0x02330b5e7f1e2c0cULL, },
        { 0xfb2f00064d240608ULL, 0x020117000007520fULL, },
        { 0x1f02000c02120108ULL, 0x040117060000140fULL, },
        { 0x0f2f001826011808ULL, 0x00f702000207020fULL, },
        { 0xfb01000301493100ULL, 0x007bbb1a0a0f14fcULL, },
        { 0xac16020ab9330480ULL, 0x0401180302052501ULL, },    /*  72  */
        { 0x1501ae1505190180ULL, 0x0901183f05000901ULL, },
        { 0x0a16022a5c011180ULL, 0x00d8030115050101ULL, },
        { 0xac00020502672202ULL, 0x016cc6ff550a0914ULL, },
        { 0x701300045e0c074eULL, 0x110111030208e20aULL, },
        { 0x0e0116090206014eULL, 0x230111360500380aULL, },
        { 0x071300132f001c4eULL, 0x01f102011508070aULL, },
        { 0x7000000201183801ULL, 0x047888d8541038a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRL_B(b128_pattern[i], b128_pattern[j],
                         b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRL_B(b128_random[i], b128_random[j],
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
