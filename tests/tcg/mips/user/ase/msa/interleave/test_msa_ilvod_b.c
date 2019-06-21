/*
 *  Test program for MSA instruction ILVOD.B
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
    char *instruction_name =  "ILVOD.B";
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
        { 0xffe3ff38ff8effe3ULL, 0xff38ff8effe3ff38ULL, },
        { 0xff1cffc7ff71ff1cULL, 0xffc7ff71ff1cffc7ULL, },
        { 0x00ff00ff00ff00ffULL, 0x00ff00ff00ff00ffULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x00aa00aa00aa00aaULL, 0x00aa00aa00aa00aaULL, },
        { 0x0055005500550055ULL, 0x0055005500550055ULL, },
        { 0x00cc00cc00cc00ccULL, 0x00cc00cc00cc00ccULL, },
        { 0x0033003300330033ULL, 0x0033003300330033ULL, },
        { 0x00e30038008e00e3ULL, 0x0038008e00e30038ULL, },
        { 0x001c00c70071001cULL, 0x00c70071001c00c7ULL, },
        { 0xaaffaaffaaffaaffULL, 0xaaffaaffaaffaaffULL, },    /*  16  */
        { 0xaa00aa00aa00aa00ULL, 0xaa00aa00aa00aa00ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaa55aa55aa55aa55ULL, 0xaa55aa55aa55aa55ULL, },
        { 0xaaccaaccaaccaaccULL, 0xaaccaaccaaccaaccULL, },
        { 0xaa33aa33aa33aa33ULL, 0xaa33aa33aa33aa33ULL, },
        { 0xaae3aa38aa8eaae3ULL, 0xaa38aa8eaae3aa38ULL, },
        { 0xaa1caac7aa71aa1cULL, 0xaac7aa71aa1caac7ULL, },
        { 0x55ff55ff55ff55ffULL, 0x55ff55ff55ff55ffULL, },    /*  24  */
        { 0x5500550055005500ULL, 0x5500550055005500ULL, },
        { 0x55aa55aa55aa55aaULL, 0x55aa55aa55aa55aaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x55cc55cc55cc55ccULL, 0x55cc55cc55cc55ccULL, },
        { 0x5533553355335533ULL, 0x5533553355335533ULL, },
        { 0x55e35538558e55e3ULL, 0x5538558e55e35538ULL, },
        { 0x551c55c75571551cULL, 0x55c75571551c55c7ULL, },
        { 0xccffccffccffccffULL, 0xccffccffccffccffULL, },    /*  32  */
        { 0xcc00cc00cc00cc00ULL, 0xcc00cc00cc00cc00ULL, },
        { 0xccaaccaaccaaccaaULL, 0xccaaccaaccaaccaaULL, },
        { 0xcc55cc55cc55cc55ULL, 0xcc55cc55cc55cc55ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xcc33cc33cc33cc33ULL, 0xcc33cc33cc33cc33ULL, },
        { 0xcce3cc38cc8ecce3ULL, 0xcc38cc8ecce3cc38ULL, },
        { 0xcc1cccc7cc71cc1cULL, 0xccc7cc71cc1cccc7ULL, },
        { 0x33ff33ff33ff33ffULL, 0x33ff33ff33ff33ffULL, },    /*  40  */
        { 0x3300330033003300ULL, 0x3300330033003300ULL, },
        { 0x33aa33aa33aa33aaULL, 0x33aa33aa33aa33aaULL, },
        { 0x3355335533553355ULL, 0x3355335533553355ULL, },
        { 0x33cc33cc33cc33ccULL, 0x33cc33cc33cc33ccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x33e33338338e33e3ULL, 0x3338338e33e33338ULL, },
        { 0x331c33c73371331cULL, 0x33c73371331c33c7ULL, },
        { 0xe3ff38ff8effe3ffULL, 0x38ff8effe3ff38ffULL, },    /*  48  */
        { 0xe30038008e00e300ULL, 0x38008e00e3003800ULL, },
        { 0xe3aa38aa8eaae3aaULL, 0x38aa8eaae3aa38aaULL, },
        { 0xe35538558e55e355ULL, 0x38558e55e3553855ULL, },
        { 0xe3cc38cc8ecce3ccULL, 0x38cc8ecce3cc38ccULL, },
        { 0xe33338338e33e333ULL, 0x38338e33e3333833ULL, },
        { 0xe3e338388e8ee3e3ULL, 0x38388e8ee3e33838ULL, },
        { 0xe31c38c78e71e31cULL, 0x38c78e71e31c38c7ULL, },
        { 0x1cffc7ff71ff1cffULL, 0xc7ff71ff1cffc7ffULL, },    /*  56  */
        { 0x1c00c70071001c00ULL, 0xc70071001c00c700ULL, },
        { 0x1caac7aa71aa1caaULL, 0xc7aa71aa1caac7aaULL, },
        { 0x1c55c75571551c55ULL, 0xc75571551c55c755ULL, },
        { 0x1cccc7cc71cc1cccULL, 0xc7cc71cc1cccc7ccULL, },
        { 0x1c33c73371331c33ULL, 0xc73371331c33c733ULL, },
        { 0x1ce3c738718e1ce3ULL, 0xc738718e1ce3c738ULL, },
        { 0x1c1cc7c771711c1cULL, 0xc7c771711c1cc7c7ULL, },
        { 0x8888e6e628285555ULL, 0x4b4b0b0bfefeb0b0ULL, },    /*  64  */
        { 0x88fbe600284d55c7ULL, 0x4b120bbbfe15b052ULL, },
        { 0x88ace6ae28b9558bULL, 0x4b270bc6feabb025ULL, },
        { 0x8870e616285e55e2ULL, 0x4b8d0b88fea9b0e2ULL, },
        { 0xfb8800e64d28c755ULL, 0x124bbb0b15fe52b0ULL, },
        { 0xfbfb00004d4dc7c7ULL, 0x1212bbbb15155252ULL, },
        { 0xfbac00ae4db9c78bULL, 0x1227bbc615ab5225ULL, },
        { 0xfb7000164d5ec7e2ULL, 0x128dbb8815a952e2ULL, },
        { 0xac88aee6b9288b55ULL, 0x274bc60babfe25b0ULL, },    /*  72  */
        { 0xacfbae00b94d8bc7ULL, 0x2712c6bbab152552ULL, },
        { 0xacacaeaeb9b98b8bULL, 0x2727c6c6abab2525ULL, },
        { 0xac70ae16b95e8be2ULL, 0x278dc688aba925e2ULL, },
        { 0x708816e65e28e255ULL, 0x8d4b880ba9fee2b0ULL, },
        { 0x70fb16005e4de2c7ULL, 0x8d1288bba915e252ULL, },
        { 0x70ac16ae5eb9e28bULL, 0x8d2788c6a9abe225ULL, },
        { 0x707016165e5ee2e2ULL, 0x8d8d8888a9a9e2e2ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_ILVOD_B(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_ILVOD_B(b128_random[i], b128_random[j],
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
