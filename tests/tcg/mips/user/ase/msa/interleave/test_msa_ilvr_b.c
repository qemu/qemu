/*
 *  Test program for MSA instruction ILVR.B
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
    char *instruction_name =  "ILVR.B";
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
        { 0xff8eff38ffe3ff8eULL, 0xffe3ff8eff38ffe3ULL, },
        { 0xff71ffc7ff1cff71ULL, 0xff1cff71ffc7ff1cULL, },
        { 0x00ff00ff00ff00ffULL, 0x00ff00ff00ff00ffULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x00aa00aa00aa00aaULL, 0x00aa00aa00aa00aaULL, },
        { 0x0055005500550055ULL, 0x0055005500550055ULL, },
        { 0x00cc00cc00cc00ccULL, 0x00cc00cc00cc00ccULL, },
        { 0x0033003300330033ULL, 0x0033003300330033ULL, },
        { 0x008e003800e3008eULL, 0x00e3008e003800e3ULL, },
        { 0x007100c7001c0071ULL, 0x001c007100c7001cULL, },
        { 0xaaffaaffaaffaaffULL, 0xaaffaaffaaffaaffULL, },    /*  16  */
        { 0xaa00aa00aa00aa00ULL, 0xaa00aa00aa00aa00ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaa55aa55aa55aa55ULL, 0xaa55aa55aa55aa55ULL, },
        { 0xaaccaaccaaccaaccULL, 0xaaccaaccaaccaaccULL, },
        { 0xaa33aa33aa33aa33ULL, 0xaa33aa33aa33aa33ULL, },
        { 0xaa8eaa38aae3aa8eULL, 0xaae3aa8eaa38aae3ULL, },
        { 0xaa71aac7aa1caa71ULL, 0xaa1caa71aac7aa1cULL, },
        { 0x55ff55ff55ff55ffULL, 0x55ff55ff55ff55ffULL, },    /*  24  */
        { 0x5500550055005500ULL, 0x5500550055005500ULL, },
        { 0x55aa55aa55aa55aaULL, 0x55aa55aa55aa55aaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x55cc55cc55cc55ccULL, 0x55cc55cc55cc55ccULL, },
        { 0x5533553355335533ULL, 0x5533553355335533ULL, },
        { 0x558e553855e3558eULL, 0x55e3558e553855e3ULL, },
        { 0x557155c7551c5571ULL, 0x551c557155c7551cULL, },
        { 0xccffccffccffccffULL, 0xccffccffccffccffULL, },    /*  32  */
        { 0xcc00cc00cc00cc00ULL, 0xcc00cc00cc00cc00ULL, },
        { 0xccaaccaaccaaccaaULL, 0xccaaccaaccaaccaaULL, },
        { 0xcc55cc55cc55cc55ULL, 0xcc55cc55cc55cc55ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xcc33cc33cc33cc33ULL, 0xcc33cc33cc33cc33ULL, },
        { 0xcc8ecc38cce3cc8eULL, 0xcce3cc8ecc38cce3ULL, },
        { 0xcc71ccc7cc1ccc71ULL, 0xcc1ccc71ccc7cc1cULL, },
        { 0x33ff33ff33ff33ffULL, 0x33ff33ff33ff33ffULL, },    /*  40  */
        { 0x3300330033003300ULL, 0x3300330033003300ULL, },
        { 0x33aa33aa33aa33aaULL, 0x33aa33aa33aa33aaULL, },
        { 0x3355335533553355ULL, 0x3355335533553355ULL, },
        { 0x33cc33cc33cc33ccULL, 0x33cc33cc33cc33ccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x338e333833e3338eULL, 0x33e3338e333833e3ULL, },
        { 0x337133c7331c3371ULL, 0x331c337133c7331cULL, },
        { 0x8eff38ffe3ff8effULL, 0xe3ff8eff38ffe3ffULL, },    /*  48  */
        { 0x8e003800e3008e00ULL, 0xe3008e003800e300ULL, },
        { 0x8eaa38aae3aa8eaaULL, 0xe3aa8eaa38aae3aaULL, },
        { 0x8e553855e3558e55ULL, 0xe3558e553855e355ULL, },
        { 0x8ecc38cce3cc8eccULL, 0xe3cc8ecc38cce3ccULL, },
        { 0x8e333833e3338e33ULL, 0xe3338e333833e333ULL, },
        { 0x8e8e3838e3e38e8eULL, 0xe3e38e8e3838e3e3ULL, },
        { 0x8e7138c7e31c8e71ULL, 0xe31c8e7138c7e31cULL, },
        { 0x71ffc7ff1cff71ffULL, 0x1cff71ffc7ff1cffULL, },    /*  56  */
        { 0x7100c7001c007100ULL, 0x1c007100c7001c00ULL, },
        { 0x71aac7aa1caa71aaULL, 0x1caa71aac7aa1caaULL, },
        { 0x7155c7551c557155ULL, 0x1c557155c7551c55ULL, },
        { 0x71ccc7cc1ccc71ccULL, 0x1ccc71ccc7cc1cccULL, },
        { 0x7133c7331c337133ULL, 0x1c337133c7331c33ULL, },
        { 0x718ec7381ce3718eULL, 0x1ce3718ec7381ce3ULL, },
        { 0x7171c7c71c1c7171ULL, 0x1c1c7171c7c71c1cULL, },
        { 0x2828626255554040ULL, 0x88886a6ae6e6ccccULL, },    /*  64  */
        { 0x284d629355c74008ULL, 0x88fb6abee600cc63ULL, },
        { 0x28b962cf558b4080ULL, 0x88ac6a5ae6aeccaaULL, },
        { 0x285e623155e2404eULL, 0x88706a4fe616cc4dULL, },
        { 0x4d289362c7550840ULL, 0xfb88be6a00e663ccULL, },
        { 0x4d4d9393c7c70808ULL, 0xfbfbbebe00006363ULL, },
        { 0x4db993cfc78b0880ULL, 0xfbacbe5a00ae63aaULL, },
        { 0x4d5e9331c7e2084eULL, 0xfb70be4f0016634dULL, },
        { 0xb928cf628b558040ULL, 0xac885a6aaee6aaccULL, },    /*  72  */
        { 0xb94dcf938bc78008ULL, 0xacfb5abeae00aa63ULL, },
        { 0xb9b9cfcf8b8b8080ULL, 0xacac5a5aaeaeaaaaULL, },
        { 0xb95ecf318be2804eULL, 0xac705a4fae16aa4dULL, },
        { 0x5e283162e2554e40ULL, 0x70884f6a16e64dccULL, },
        { 0x5e4d3193e2c74e08ULL, 0x70fb4fbe16004d63ULL, },
        { 0x5eb931cfe28b4e80ULL, 0x70ac4f5a16ae4daaULL, },
        { 0x5e5e3131e2e24e4eULL, 0x70704f4f16164d4dULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_ILVR_B(b128_pattern[i], b128_pattern[j],
                          b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_ILVR_B(b128_random[i], b128_random[j],
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
