/*
 *  Test program for MSA instruction PCNT.W
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
    char *instruction_name =  "PCNT.W";
    int32_t ret;
    uint32_t i;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000002000000020ULL, 0x0000002000000020ULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000001000000010ULL, 0x0000001000000010ULL, },
        { 0x0000001000000010ULL, 0x0000001000000010ULL, },
        { 0x0000001000000010ULL, 0x0000001000000010ULL, },
        { 0x0000001000000010ULL, 0x0000001000000010ULL, },
        { 0x0000001100000010ULL, 0x0000000f00000011ULL, },
        { 0x0000000f00000010ULL, 0x000000110000000fULL, },
        { 0x0000001000000010ULL, 0x0000001000000010ULL, },    /*   8  */
        { 0x0000001000000010ULL, 0x0000001000000010ULL, },
        { 0x0000001100000011ULL, 0x000000100000000fULL, },
        { 0x0000000f0000000fULL, 0x0000001000000011ULL, },
        { 0x0000001200000010ULL, 0x0000000e00000012ULL, },
        { 0x0000000e00000010ULL, 0x000000120000000eULL, },
        { 0x0000001200000011ULL, 0x0000000e00000010ULL, },
        { 0x0000000e0000000fULL, 0x0000001200000010ULL, },
        { 0x0000001000000010ULL, 0x0000001000000010ULL, },    /*  16  */
        { 0x0000001000000010ULL, 0x0000001000000010ULL, },
        { 0x0000001200000012ULL, 0x0000000f0000000eULL, },
        { 0x0000000e0000000eULL, 0x0000001100000012ULL, },
        { 0x000000140000000eULL, 0x0000001000000012ULL, },
        { 0x0000000c00000012ULL, 0x000000100000000eULL, },
        { 0x000000150000000cULL, 0x000000130000000eULL, },
        { 0x0000000b00000014ULL, 0x0000000d00000012ULL, },
        { 0x0000001400000010ULL, 0x0000000c00000014ULL, },    /*  24  */
        { 0x0000000c00000010ULL, 0x000000140000000cULL, },
        { 0x0000001300000013ULL, 0x0000000e0000000dULL, },
        { 0x0000000d0000000dULL, 0x0000001200000013ULL, },
        { 0x0000001200000012ULL, 0x0000001200000010ULL, },
        { 0x0000000e0000000eULL, 0x0000000e00000010ULL, },
        { 0x0000001100000011ULL, 0x0000001100000011ULL, },
        { 0x0000000f0000000fULL, 0x0000000f0000000fULL, },
        { 0x0000001000000010ULL, 0x0000001000000010ULL, },    /*  32  */
        { 0x0000001000000010ULL, 0x0000001000000010ULL, },
        { 0x0000001100000011ULL, 0x0000001100000011ULL, },
        { 0x0000000f0000000fULL, 0x0000000f0000000fULL, },
        { 0x0000001200000012ULL, 0x0000001200000012ULL, },
        { 0x0000000e0000000eULL, 0x0000000e0000000eULL, },
        { 0x0000001300000013ULL, 0x000000130000000eULL, },
        { 0x0000000d0000000dULL, 0x0000000d00000012ULL, },
        { 0x0000001400000014ULL, 0x000000100000000cULL, },    /*  40  */
        { 0x0000000c0000000cULL, 0x0000001000000014ULL, },
        { 0x0000001500000015ULL, 0x0000000c0000000bULL, },
        { 0x0000000b0000000bULL, 0x0000001400000015ULL, },
        { 0x0000001600000014ULL, 0x0000000a0000000eULL, },
        { 0x0000000a0000000cULL, 0x0000001600000012ULL, },
        { 0x0000001700000012ULL, 0x0000000900000013ULL, },
        { 0x000000090000000eULL, 0x000000170000000dULL, },
        { 0x0000001800000010ULL, 0x0000000800000018ULL, },    /*  48  */
        { 0x0000000800000010ULL, 0x0000001800000008ULL, },
        { 0x000000190000000eULL, 0x0000000b00000019ULL, },
        { 0x0000000700000012ULL, 0x0000001500000007ULL, },
        { 0x0000001a0000000cULL, 0x0000000e00000018ULL, },
        { 0x0000000600000014ULL, 0x0000001200000008ULL, },
        { 0x0000001b0000000aULL, 0x0000001100000014ULL, },
        { 0x0000000500000016ULL, 0x0000000f0000000cULL, },
        { 0x0000001c00000008ULL, 0x0000001400000010ULL, },    /*  56  */
        { 0x0000000400000018ULL, 0x0000000c00000010ULL, },
        { 0x0000001d00000006ULL, 0x000000170000000cULL, },
        { 0x000000030000001aULL, 0x0000000900000014ULL, },
        { 0x0000001e00000004ULL, 0x0000001a00000008ULL, },
        { 0x000000020000001cULL, 0x0000000600000018ULL, },
        { 0x0000001f00000002ULL, 0x0000001d00000004ULL, },
        { 0x000000010000001eULL, 0x000000030000001cULL, },
        { 0x0000000f0000000aULL, 0x0000001100000012ULL, },    /*  64  */
        { 0x000000110000000eULL, 0x0000001200000012ULL, },
        { 0x0000001100000010ULL, 0x000000140000000eULL, },
        { 0x0000000f00000010ULL, 0x0000000f0000000cULL, },
        { 0x0000001200000011ULL, 0x0000000f00000013ULL, },
        { 0x0000001000000014ULL, 0x0000000b00000011ULL, },
        { 0x0000000e00000010ULL, 0x0000000900000013ULL, },
        { 0x0000001200000010ULL, 0x000000140000000eULL, },
        { 0x0000000c00000011ULL, 0x000000130000000fULL, },    /*  72  */
        { 0x0000001100000014ULL, 0x0000000e00000010ULL, },
        { 0x0000001300000011ULL, 0x0000000e0000000fULL, },
        { 0x000000110000000eULL, 0x0000001100000013ULL, },
        { 0x0000000f0000000eULL, 0x0000000e00000010ULL, },
        { 0x000000110000000cULL, 0x0000001100000010ULL, },
        { 0x0000000b00000013ULL, 0x000000140000000eULL, },
        { 0x0000001000000014ULL, 0x0000001200000010ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < TEST_COUNT_TOTAL; i++) {
        if (i < PATTERN_INPUTS_COUNT) {
            do_msa_PCNT_W(b128_pattern[i], b128_result[i]);
        } else {
            do_msa_PCNT_W(b128_random[i - PATTERN_INPUTS_COUNT],
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
