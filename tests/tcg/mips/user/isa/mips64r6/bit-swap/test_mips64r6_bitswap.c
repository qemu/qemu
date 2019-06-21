/*
 *  Test program for MIPS64R6 instruction BITSWAP
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

#include "../../../../include/wrappers_mips64r6.h"
#include "../../../../include/test_inputs_64.h"
#include "../../../../include/test_utils_64.h"

#define TEST_COUNT_TOTAL (PATTERN_INPUTS_64_COUNT + RANDOM_INPUTS_64_COUNT)


int32_t main(void)
{
    char *isa_ase_name = "mips64r6";
    char *group_name = "Bit Swap";
    char *instruction_name =   "BITSWAP";
    int32_t ret;
    uint32_t i;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b64_result[TEST_COUNT_TOTAL];
    uint64_t b64_expect[TEST_COUNT_TOTAL] = {
        0xffffffffffffffffULL,                    /*   0  */
        0x0000000000000000ULL,
        0x0000000055555555ULL,
        0xffffffffaaaaaaaaULL,
        0x0000000033333333ULL,
        0xffffffffccccccccULL,
        0x00000000711cc771ULL,
        0xffffffff8ee3388eULL,
        0x000000000f0f0f0fULL,                    /*   8  */
        0xfffffffff0f0f0f0ULL,
        0x00000000071f7cf0ULL,
        0xfffffffff8e0830fULL,
        0xfffffffff0033ff0ULL,
        0x000000000ffcc00fULL,
        0x0000000007fc017fULL,
        0xfffffffff803fe80ULL,
        0xffffffffff00ff00ULL,                    /*  16  */
        0x0000000000ff00ffULL,
        0xfffffffff01fc07fULL,
        0x000000000fe03f80ULL,
        0x0000000000ff03f0ULL,
        0xffffffffff00fc0fULL,
        0x0000000001f07f00ULL,
        0xfffffffffe0f80ffULL,
        0x000000000f00ff0fULL,                    /*  24  */
        0xfffffffff0ff00f0ULL,
        0x000000007f00f0ffULL,
        0xffffffff80ff0f00ULL,
        0xffffffffff0300ffULL,
        0x0000000000fcff00ULL,
        0xffffffffff1f00f0ULL,
        0x0000000000e0ff0fULL,
        0xffffffffffff0000ULL,                    /*  32  */
        0x000000000000ffffULL,
        0xfffffffffcff0700ULL,
        0x000000000300f8ffULL,
        0xfffffffff0ff3f00ULL,
        0x000000000f00c0ffULL,
        0xffffffffc0ffff01ULL,
        0x000000003f0000feULL,
        0x0000000000ffff0fULL,                    /*  40  */
        0xffffffffff0000f0ULL,
        0x0000000000fcff7fULL,
        0xffffffffff030080ULL,
        0x0000000000f0ffffULL,
        0xffffffffff0f0000ULL,
        0x0000000000c0ffffULL,
        0xffffffffff3f0000ULL,
        0x000000000000ffffULL,                    /*  48  */
        0xffffffffffff0000ULL,
        0x000000000000fcffULL,
        0xffffffffffff0300ULL,
        0x000000000000f0ffULL,
        0xffffffffffff0f00ULL,
        0x000000000000c0ffULL,
        0xffffffffffff3f00ULL,
        0x00000000000000ffULL,                    /*  56  */
        0xffffffffffffff00ULL,
        0x00000000000000fcULL,
        0xffffffffffffff03ULL,
        0x00000000000000f0ULL,
        0xffffffffffffff0fULL,
        0x00000000000000c0ULL,
        0xffffffffffffff3fULL,
        0x000000001446aa02ULL,                    /*  64  */
        0xffffffffb2c9e310ULL,
        0xffffffff9df3d101ULL,
        0x000000007a8c4772ULL,
        0xffffffffbef5421aULL,
        0xffffffffff50749fULL,
        0xffffffffa6533d52ULL,
        0x000000005965ed41ULL,
        0x000000006a756792ULL,                    /*  72  */
        0xffffffffa69ba7ebULL,
        0xffffffff93d363d8ULL,
        0xffffffff8c152675ULL,
        0x00000000654a5750ULL,
        0xffffffff98c48615ULL,
        0x00000000447def39ULL,
        0x000000004f9a7bb5ULL,
    };

    gettimeofday(&start, NULL);

    for (i = 0; i < TEST_COUNT_TOTAL; i++) {
        if (i < PATTERN_INPUTS_64_COUNT) {
            do_mips64r6_BITSWAP(b64_pattern + i, b64_result + i);
        } else {
            do_mips64r6_BITSWAP(b64_random + (i - PATTERN_INPUTS_64_COUNT),
                                b64_result + i);
        }
    }

    gettimeofday(&end, NULL);

    elapsed_time = (end.tv_sec - start.tv_sec) * 1000.0;
    elapsed_time += (end.tv_usec - start.tv_usec) / 1000.0;

    ret = check_results_64(instruction_name, TEST_COUNT_TOTAL, elapsed_time,
                           b64_result, b64_expect);

    return ret;
}
