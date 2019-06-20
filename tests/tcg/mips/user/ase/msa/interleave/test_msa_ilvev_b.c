/*
 *  Test program for MSA instruction ILVEV.B
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
    char *instruction_name =  "ILVEV.B";
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
        { 0xff8effe3ff38ff8eULL, 0xffe3ff38ff8effe3ULL, },
        { 0xff71ff1cffc7ff71ULL, 0xff1cffc7ff71ff1cULL, },
        { 0x00ff00ff00ff00ffULL, 0x00ff00ff00ff00ffULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x00aa00aa00aa00aaULL, 0x00aa00aa00aa00aaULL, },
        { 0x0055005500550055ULL, 0x0055005500550055ULL, },
        { 0x00cc00cc00cc00ccULL, 0x00cc00cc00cc00ccULL, },
        { 0x0033003300330033ULL, 0x0033003300330033ULL, },
        { 0x008e00e30038008eULL, 0x00e30038008e00e3ULL, },
        { 0x0071001c00c70071ULL, 0x001c00c70071001cULL, },
        { 0xaaffaaffaaffaaffULL, 0xaaffaaffaaffaaffULL, },    /*  16  */
        { 0xaa00aa00aa00aa00ULL, 0xaa00aa00aa00aa00ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaa55aa55aa55aa55ULL, 0xaa55aa55aa55aa55ULL, },
        { 0xaaccaaccaaccaaccULL, 0xaaccaaccaaccaaccULL, },
        { 0xaa33aa33aa33aa33ULL, 0xaa33aa33aa33aa33ULL, },
        { 0xaa8eaae3aa38aa8eULL, 0xaae3aa38aa8eaae3ULL, },
        { 0xaa71aa1caac7aa71ULL, 0xaa1caac7aa71aa1cULL, },
        { 0x55ff55ff55ff55ffULL, 0x55ff55ff55ff55ffULL, },    /*  24  */
        { 0x5500550055005500ULL, 0x5500550055005500ULL, },
        { 0x55aa55aa55aa55aaULL, 0x55aa55aa55aa55aaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x55cc55cc55cc55ccULL, 0x55cc55cc55cc55ccULL, },
        { 0x5533553355335533ULL, 0x5533553355335533ULL, },
        { 0x558e55e35538558eULL, 0x55e35538558e55e3ULL, },
        { 0x5571551c55c75571ULL, 0x551c55c75571551cULL, },
        { 0xccffccffccffccffULL, 0xccffccffccffccffULL, },    /*  32  */
        { 0xcc00cc00cc00cc00ULL, 0xcc00cc00cc00cc00ULL, },
        { 0xccaaccaaccaaccaaULL, 0xccaaccaaccaaccaaULL, },
        { 0xcc55cc55cc55cc55ULL, 0xcc55cc55cc55cc55ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xcc33cc33cc33cc33ULL, 0xcc33cc33cc33cc33ULL, },
        { 0xcc8ecce3cc38cc8eULL, 0xcce3cc38cc8ecce3ULL, },
        { 0xcc71cc1cccc7cc71ULL, 0xcc1cccc7cc71cc1cULL, },
        { 0x33ff33ff33ff33ffULL, 0x33ff33ff33ff33ffULL, },    /*  40  */
        { 0x3300330033003300ULL, 0x3300330033003300ULL, },
        { 0x33aa33aa33aa33aaULL, 0x33aa33aa33aa33aaULL, },
        { 0x3355335533553355ULL, 0x3355335533553355ULL, },
        { 0x33cc33cc33cc33ccULL, 0x33cc33cc33cc33ccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x338e33e33338338eULL, 0x33e33338338e33e3ULL, },
        { 0x3371331c33c73371ULL, 0x331c33c73371331cULL, },
        { 0x8effe3ff38ff8effULL, 0xe3ff38ff8effe3ffULL, },    /*  48  */
        { 0x8e00e30038008e00ULL, 0xe30038008e00e300ULL, },
        { 0x8eaae3aa38aa8eaaULL, 0xe3aa38aa8eaae3aaULL, },
        { 0x8e55e35538558e55ULL, 0xe35538558e55e355ULL, },
        { 0x8ecce3cc38cc8eccULL, 0xe3cc38cc8ecce3ccULL, },
        { 0x8e33e33338338e33ULL, 0xe33338338e33e333ULL, },
        { 0x8e8ee3e338388e8eULL, 0xe3e338388e8ee3e3ULL, },
        { 0x8e71e31c38c78e71ULL, 0xe31c38c78e71e31cULL, },
        { 0x71ff1cffc7ff71ffULL, 0x1cffc7ff71ff1cffULL, },    /*  56  */
        { 0x71001c00c7007100ULL, 0x1c00c70071001c00ULL, },
        { 0x71aa1caac7aa71aaULL, 0x1caac7aa71aa1caaULL, },
        { 0x71551c55c7557155ULL, 0x1c55c75571551c55ULL, },
        { 0x71cc1cccc7cc71ccULL, 0x1cccc7cc71cc1cccULL, },
        { 0x71331c33c7337133ULL, 0x1c33c73371331c33ULL, },
        { 0x718e1ce3c738718eULL, 0x1ce3c738718e1ce3ULL, },
        { 0x71711c1cc7c77171ULL, 0x1c1cc7c771711c1cULL, },
        { 0x6a6acccc62624040ULL, 0x67675e5e7b7b0c0cULL, },    /*  64  */
        { 0x6abecc6362934008ULL, 0x67f75e1a7b3f0cfcULL, },
        { 0x6a5accaa62cf4080ULL, 0x67d85eff7b2b0c14ULL, },
        { 0x6a4fcc4d6231404eULL, 0x67f15ed87b420ca0ULL, },
        { 0xbe6a63cc93620840ULL, 0xf7671a5e3f7bfc0cULL, },
        { 0xbebe636393930808ULL, 0xf7f71a1a3f3ffcfcULL, },
        { 0xbe5a63aa93cf0880ULL, 0xf7d81aff3f2bfc14ULL, },
        { 0xbe4f634d9331084eULL, 0xf7f11ad83f42fca0ULL, },
        { 0x5a6aaacccf628040ULL, 0xd867ff5e2b7b140cULL, },    /*  72  */
        { 0x5abeaa63cf938008ULL, 0xd8f7ff1a2b3f14fcULL, },
        { 0x5a5aaaaacfcf8080ULL, 0xd8d8ffff2b2b1414ULL, },
        { 0x5a4faa4dcf31804eULL, 0xd8f1ffd82b4214a0ULL, },
        { 0x4f6a4dcc31624e40ULL, 0xf167d85e427ba00cULL, },
        { 0x4fbe4d6331934e08ULL, 0xf1f7d81a423fa0fcULL, },
        { 0x4f5a4daa31cf4e80ULL, 0xf1d8d8ff422ba014ULL, },
        { 0x4f4f4d4d31314e4eULL, 0xf1f1d8d84242a0a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_ILVEV_B(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_ILVEV_B(b128_random[i], b128_random[j],
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
