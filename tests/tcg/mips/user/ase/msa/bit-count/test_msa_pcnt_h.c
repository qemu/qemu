/*
 *  Test program for MSA instruction PCNT.H
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

#define TEST_COUNT_TOTAL (PATTERN_INPUTS_COUNT + RANDOM_INPUTS_COUNT)


int32_t main(void)
{
    char *isa_ase_name = "MSA";
    char *group_name = "Bit Count";
    char *instruction_name =  "PCNT.H";
    int32_t ret;
    uint32_t i;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0010001000100010ULL, 0x0010001000100010ULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0008000800080008ULL, 0x0008000800080008ULL, },
        { 0x0008000800080008ULL, 0x0008000800080008ULL, },
        { 0x0008000800080008ULL, 0x0008000800080008ULL, },
        { 0x0008000800080008ULL, 0x0008000800080008ULL, },
        { 0x0009000800070009ULL, 0x0008000700090008ULL, },
        { 0x0007000800090007ULL, 0x0008000900070008ULL, },
        { 0x0008000800080008ULL, 0x0008000800080008ULL, },    /*   8  */
        { 0x0008000800080008ULL, 0x0008000800080008ULL, },
        { 0x000a000700080009ULL, 0x0006000a00070008ULL, },
        { 0x0006000900080007ULL, 0x000a000600090008ULL, },
        { 0x000a00080006000aULL, 0x00080006000a0008ULL, },
        { 0x00060008000a0006ULL, 0x0008000a00060008ULL, },
        { 0x0009000900090008ULL, 0x0007000700070009ULL, },
        { 0x0007000700070008ULL, 0x0009000900090007ULL, },
        { 0x0008000800080008ULL, 0x0008000800080008ULL, },    /*  16  */
        { 0x0008000800080008ULL, 0x0008000800080008ULL, },
        { 0x0009000900090009ULL, 0x0008000700070007ULL, },
        { 0x0007000700070007ULL, 0x0008000900090009ULL, },
        { 0x000a000a00080006ULL, 0x0006000a000a0008ULL, },
        { 0x000600060008000aULL, 0x000a000600060008ULL, },
        { 0x000b000a00050007ULL, 0x000b000800050009ULL, },
        { 0x00050006000b0009ULL, 0x00050008000b0007ULL, },
        { 0x000c00080004000cULL, 0x00080004000c0008ULL, },    /*  24  */
        { 0x00040008000c0004ULL, 0x0008000c00040008ULL, },
        { 0x000d00060007000cULL, 0x0003000b00080005ULL, },
        { 0x0003000a00090004ULL, 0x000d00050008000bULL, },
        { 0x000e0004000a0008ULL, 0x0006000c0002000eULL, },
        { 0x0002000c00060008ULL, 0x000a0004000e0002ULL, },
        { 0x000f0002000d0004ULL, 0x000b000600090008ULL, },
        { 0x0001000e0003000cULL, 0x0005000a00070008ULL, },
        { 0x0010000000100000ULL, 0x0010000000100000ULL, },    /*  32  */
        { 0x0000001000000010ULL, 0x0000001000000010ULL, },
        { 0x00100001000e0003ULL, 0x000c0005000a0007ULL, },
        { 0x0000000f0002000dULL, 0x0004000b00060009ULL, },
        { 0x00100002000c0006ULL, 0x0008000a0004000eULL, },
        { 0x0000000e0004000aULL, 0x00080006000c0002ULL, },
        { 0x00100003000a0009ULL, 0x0004000f0000000eULL, },
        { 0x0000000d00060007ULL, 0x000c000100100002ULL, },
        { 0x001000040008000cULL, 0x0000001000040008ULL, },    /*  40  */
        { 0x0000000c00080004ULL, 0x00100000000c0008ULL, },
        { 0x001000050006000fULL, 0x0000000c00090002ULL, },
        { 0x0000000b000a0001ULL, 0x001000040007000eULL, },
        { 0x0010000600040010ULL, 0x00020008000e0000ULL, },
        { 0x0000000a000c0000ULL, 0x000e000800020010ULL, },
        { 0x0010000700020010ULL, 0x0005000400100003ULL, },
        { 0x00000009000e0000ULL, 0x000b000c0000000dULL, },
        { 0x0010000800000010ULL, 0x0008000000100008ULL, },    /*  48  */
        { 0x0000000800100000ULL, 0x0008001000000008ULL, },
        { 0x001000090000000eULL, 0x000b0000000c000dULL, },
        { 0x0000000700100002ULL, 0x0005001000040003ULL, },
        { 0x0010000a0000000cULL, 0x000e000000080010ULL, },
        { 0x0000000600100004ULL, 0x0002001000080000ULL, },
        { 0x0010000b0000000aULL, 0x0010000100040010ULL, },
        { 0x0000000500100006ULL, 0x0000000f000c0000ULL, },
        { 0x0010000c00000008ULL, 0x0010000400000010ULL, },    /*  56  */
        { 0x0000000400100008ULL, 0x0000000c00100000ULL, },
        { 0x0010000d00000006ULL, 0x001000070000000cULL, },
        { 0x000000030010000aULL, 0x0000000900100004ULL, },
        { 0x0010000e00000004ULL, 0x0010000a00000008ULL, },
        { 0x000000020010000cULL, 0x0000000600100008ULL, },
        { 0x0010000f00000002ULL, 0x0010000d00000004ULL, },
        { 0x000000010010000eULL, 0x000000030010000cULL, },
        { 0x0006000900050005ULL, 0x00090008000d0005ULL, },    /*  64  */
        { 0x000d000400080006ULL, 0x0009000900090009ULL, },
        { 0x00080009000b0005ULL, 0x0008000c00090005ULL, },
        { 0x0008000700080008ULL, 0x0009000600060006ULL, },
        { 0x0008000a000c0005ULL, 0x0005000a000a0009ULL, },
        { 0x00070009000a000aULL, 0x00070004000b0006ULL, },
        { 0x0009000500080008ULL, 0x00060003000b0008ULL, },
        { 0x000b000700080008ULL, 0x000b00090004000aULL, },
        { 0x0005000700090008ULL, 0x000c000700080007ULL, },    /*  72  */
        { 0x000900080009000bULL, 0x0006000800070009ULL, },
        { 0x0007000c00090008ULL, 0x0007000700080007ULL, },
        { 0x0007000a00060008ULL, 0x00080009000a0009ULL, },
        { 0x0007000800070007ULL, 0x0006000800090007ULL, },
        { 0x0006000b00060006ULL, 0x0009000800070009ULL, },
        { 0x000500060008000bULL, 0x000a000a00080006ULL, },
        { 0x000800080009000bULL, 0x0008000a00070009ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < TEST_COUNT_TOTAL; i++) {
        if (i < PATTERN_INPUTS_COUNT) {
            do_msa_PCNT_H(b128_pattern[i], b128_result[i]);
        } else {
            do_msa_PCNT_H(b128_random[i - PATTERN_INPUTS_COUNT],
                          b128_result[i]);
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
