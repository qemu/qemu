/*
 *  Test program for MSA instruction ILVL.B
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
    char *group_name = "Interleave";
    char *instruction_name =  "ILVL.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0xff00ff00ff00ff00ULL, 0xff00ff00ff00ff00ULL, },
        { 0xffaaffaaffaaffaaULL, 0xffaaffaaffaaffaaULL, },
        { 0xff55ff55ff55ff55ULL, 0xff55ff55ff55ff55ULL, },
        { 0xffccffccffccffccULL, 0xffccffccffccffccULL, },
        { 0xff33ff33ff33ff33ULL, 0xff33ff33ff33ff33ULL, },
        { 0xffe3ff8eff38ffe3ULL, 0xff38ffe3ff8eff38ULL, },
        { 0xff1cff71ffc7ff1cULL, 0xffc7ff1cff71ffc7ULL, },
        { 0x00ff00ff00ff00ffULL, 0x00ff00ff00ff00ffULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x00aa00aa00aa00aaULL, 0x00aa00aa00aa00aaULL, },
        { 0x0055005500550055ULL, 0x0055005500550055ULL, },
        { 0x00cc00cc00cc00ccULL, 0x00cc00cc00cc00ccULL, },
        { 0x0033003300330033ULL, 0x0033003300330033ULL, },
        { 0x00e3008e003800e3ULL, 0x003800e3008e0038ULL, },
        { 0x001c007100c7001cULL, 0x00c7001c007100c7ULL, },
        { 0xaaffaaffaaffaaffULL, 0xaaffaaffaaffaaffULL, },    /*  16  */
        { 0xaa00aa00aa00aa00ULL, 0xaa00aa00aa00aa00ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaa55aa55aa55aa55ULL, 0xaa55aa55aa55aa55ULL, },
        { 0xaaccaaccaaccaaccULL, 0xaaccaaccaaccaaccULL, },
        { 0xaa33aa33aa33aa33ULL, 0xaa33aa33aa33aa33ULL, },
        { 0xaae3aa8eaa38aae3ULL, 0xaa38aae3aa8eaa38ULL, },
        { 0xaa1caa71aac7aa1cULL, 0xaac7aa1caa71aac7ULL, },
        { 0x55ff55ff55ff55ffULL, 0x55ff55ff55ff55ffULL, },    /*  24  */
        { 0x5500550055005500ULL, 0x5500550055005500ULL, },
        { 0x55aa55aa55aa55aaULL, 0x55aa55aa55aa55aaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x55cc55cc55cc55ccULL, 0x55cc55cc55cc55ccULL, },
        { 0x5533553355335533ULL, 0x5533553355335533ULL, },
        { 0x55e3558e553855e3ULL, 0x553855e3558e5538ULL, },
        { 0x551c557155c7551cULL, 0x55c7551c557155c7ULL, },
        { 0xccffccffccffccffULL, 0xccffccffccffccffULL, },    /*  32  */
        { 0xcc00cc00cc00cc00ULL, 0xcc00cc00cc00cc00ULL, },
        { 0xccaaccaaccaaccaaULL, 0xccaaccaaccaaccaaULL, },
        { 0xcc55cc55cc55cc55ULL, 0xcc55cc55cc55cc55ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xcc33cc33cc33cc33ULL, 0xcc33cc33cc33cc33ULL, },
        { 0xcce3cc8ecc38cce3ULL, 0xcc38cce3cc8ecc38ULL, },
        { 0xcc1ccc71ccc7cc1cULL, 0xccc7cc1ccc71ccc7ULL, },
        { 0x33ff33ff33ff33ffULL, 0x33ff33ff33ff33ffULL, },    /*  40  */
        { 0x3300330033003300ULL, 0x3300330033003300ULL, },
        { 0x33aa33aa33aa33aaULL, 0x33aa33aa33aa33aaULL, },
        { 0x3355335533553355ULL, 0x3355335533553355ULL, },
        { 0x33cc33cc33cc33ccULL, 0x33cc33cc33cc33ccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x33e3338e333833e3ULL, 0x333833e3338e3338ULL, },
        { 0x331c337133c7331cULL, 0x33c7331c337133c7ULL, },
        { 0xe3ff8eff38ffe3ffULL, 0x38ffe3ff8eff38ffULL, },    /*  48  */
        { 0xe3008e003800e300ULL, 0x3800e3008e003800ULL, },
        { 0xe3aa8eaa38aae3aaULL, 0x38aae3aa8eaa38aaULL, },
        { 0xe3558e553855e355ULL, 0x3855e3558e553855ULL, },
        { 0xe3cc8ecc38cce3ccULL, 0x38cce3cc8ecc38ccULL, },
        { 0xe3338e333833e333ULL, 0x3833e3338e333833ULL, },
        { 0xe3e38e8e3838e3e3ULL, 0x3838e3e38e8e3838ULL, },
        { 0xe31c8e7138c7e31cULL, 0x38c7e31c8e7138c7ULL, },
        { 0x1cff71ffc7ff1cffULL, 0xc7ff1cff71ffc7ffULL, },    /*  56  */
        { 0x1c007100c7001c00ULL, 0xc7001c007100c700ULL, },
        { 0x1caa71aac7aa1caaULL, 0xc7aa1caa71aac7aaULL, },
        { 0x1c557155c7551c55ULL, 0xc7551c557155c755ULL, },
        { 0x1ccc71ccc7cc1cccULL, 0xc7cc1ccc71ccc7ccULL, },
        { 0x1c337133c7331c33ULL, 0xc7331c337133c733ULL, },
        { 0x1ce3718ec7381ce3ULL, 0xc7381ce3718ec738ULL, },
        { 0x1c1c7171c7c71c1cULL, 0xc7c71c1c7171c7c7ULL, },
        { 0xfefe7b7bb0b00c0cULL, 0x4b4b67670b0b5e5eULL, },    /*  64  */
        { 0xfe157b3fb0520cfcULL, 0x4b1267f70bbb5e1aULL, },
        { 0xfeab7b2bb0250c14ULL, 0x4b2767d80bc65effULL, },
        { 0xfea97b42b0e20ca0ULL, 0x4b8d67f10b885ed8ULL, },
        { 0x15fe3f7b52b0fc0cULL, 0x124bf767bb0b1a5eULL, },
        { 0x15153f3f5252fcfcULL, 0x1212f7f7bbbb1a1aULL, },
        { 0x15ab3f2b5225fc14ULL, 0x1227f7d8bbc61affULL, },
        { 0x15a93f4252e2fca0ULL, 0x128df7f1bb881ad8ULL, },
        { 0xabfe2b7b25b0140cULL, 0x274bd867c60bff5eULL, },    /*  72  */
        { 0xab152b3f255214fcULL, 0x2712d8f7c6bbff1aULL, },
        { 0xabab2b2b25251414ULL, 0x2727d8d8c6c6ffffULL, },
        { 0xaba92b4225e214a0ULL, 0x278dd8f1c688ffd8ULL, },
        { 0xa9fe427be2b0a00cULL, 0x8d4bf167880bd85eULL, },
        { 0xa915423fe252a0fcULL, 0x8d12f1f788bbd81aULL, },
        { 0xa9ab422be225a014ULL, 0x8d27f1d888c6d8ffULL, },
        { 0xa9a94242e2e2a0a0ULL, 0x8d8df1f18888d8d8ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_ILVL_B(b128_pattern[i], b128_pattern[j],
                          b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_ILVL_B(b128_random[i], b128_random[j],
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
