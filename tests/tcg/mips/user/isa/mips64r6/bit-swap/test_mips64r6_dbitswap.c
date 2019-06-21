/*
 *  Test program for MIPS64R6 instruction DBITSWAP
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
    char *instruction_name =   "DBITSWAP";
    int32_t ret;
    uint32_t i;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b64_result[TEST_COUNT_TOTAL];
    uint64_t b64_expect[TEST_COUNT_TOTAL] = {
        0xffffffffffffffffULL,                    /*   0  */
        0x0000000000000000ULL,
        0x5555555555555555ULL,
        0xaaaaaaaaaaaaaaaaULL,
        0x3333333333333333ULL,
        0xccccccccccccccccULL,
        0xc7711cc7711cc771ULL,
        0x388ee3388ee3388eULL,
        0x0f0f0f0f0f0f0f0fULL,                    /*   8  */
        0xf0f0f0f0f0f0f0f0ULL,
        0x1f7cf0c1071f7cf0ULL,
        0xe0830f3ef8e0830fULL,
        0x3ff0033ff0033ff0ULL,
        0xc00ffcc00ffcc00fULL,
        0x7fc01ff007fc017fULL,
        0x803fe00ff803fe80ULL,
        0xff00ff00ff00ff00ULL,                    /*  16  */
        0x00ff00ff00ff00ffULL,
        0xff01fc07f01fc07fULL,
        0x00fe03f80fe03f80ULL,
        0xff03f03f00ff03f0ULL,
        0x00fc0fc0ff00fc0fULL,
        0xff07c0ff01f07f00ULL,
        0x00f83f00fe0f80ffULL,
        0xff0f00ff0f00ff0fULL,                    /*  24  */
        0x00f0ff00f0ff00f0ULL,
        0xff1f00fc7f00f0ffULL,
        0x00e0ff0380ff0f00ULL,
        0xff3f00f0ff0300ffULL,
        0x00c0ff0f00fcff00ULL,
        0xff7f00c0ff1f00f0ULL,
        0x0080ff3f00e0ff0fULL,
        0xffff0000ffff0000ULL,                    /*  32  */
        0x0000ffff0000ffffULL,
        0xffff0100fcff0700ULL,
        0x0000feff0300f8ffULL,
        0xffff0300f0ff3f00ULL,
        0x0000fcff0f00c0ffULL,
        0xffff0700c0ffff01ULL,
        0x0000f8ff3f0000feULL,
        0xffff0f0000ffff0fULL,                    /*  40  */
        0x0000f0ffff0000f0ULL,
        0xffff1f0000fcff7fULL,
        0x0000e0ffff030080ULL,
        0xffff3f0000f0ffffULL,
        0x0000c0ffff0f0000ULL,
        0xffff7f0000c0ffffULL,
        0x000080ffff3f0000ULL,
        0xffffff000000ffffULL,                    /*  48  */
        0x000000ffffff0000ULL,
        0xffffff010000fcffULL,
        0x000000feffff0300ULL,
        0xffffff030000f0ffULL,
        0x000000fcffff0f00ULL,
        0xffffff070000c0ffULL,
        0x000000f8ffff3f00ULL,
        0xffffff0f000000ffULL,                    /*  56  */
        0x000000f0ffffff00ULL,
        0xffffff1f000000fcULL,
        0x000000e0ffffff03ULL,
        0xffffff3f000000f0ULL,
        0x000000c0ffffff0fULL,
        0xffffff7f000000c0ULL,
        0x00000080ffffff3fULL,
        0x115667331446aa02ULL,                    /*  64  */
        0xdf7d00c6b2c9e310ULL,
        0x355a75559df3d101ULL,
        0x0ef268b27a8c4772ULL,
        0x9d49d63ebef5421aULL,
        0x0be47d91ff50749fULL,
        0x1ddc1a60a6533d52ULL,
        0x3ff1c40f5965ed41ULL,
        0x047890b36a756792ULL,                    /*  72  */
        0xa53e9bc8a69ba7ebULL,
        0x45176faf93d363d8ULL,
        0x15394f8f8c152675ULL,
        0x67281c97654a5750ULL,
        0x2952acbf98c48615ULL,
        0x620c42c6447def39ULL,
        0xd15ae5454f9a7bb5ULL,
    };

    gettimeofday(&start, NULL);

    for (i = 0; i < TEST_COUNT_TOTAL; i++) {
        if (i < PATTERN_INPUTS_64_COUNT) {
            do_mips64r6_DBITSWAP(b64_pattern + i, b64_result + i);
        } else {
            do_mips64r6_DBITSWAP(b64_random + (i - PATTERN_INPUTS_64_COUNT),
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
