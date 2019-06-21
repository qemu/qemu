/*
 *  Test program for MSA instruction ILVOD.W
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
    char *instruction_name =  "ILVOD.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0xffffffff00000000ULL, 0xffffffff00000000ULL, },
        { 0xffffffffaaaaaaaaULL, 0xffffffffaaaaaaaaULL, },
        { 0xffffffff55555555ULL, 0xffffffff55555555ULL, },
        { 0xffffffffccccccccULL, 0xffffffffccccccccULL, },
        { 0xffffffff33333333ULL, 0xffffffff33333333ULL, },
        { 0xffffffffe38e38e3ULL, 0xffffffff38e38e38ULL, },
        { 0xffffffff1c71c71cULL, 0xffffffffc71c71c7ULL, },
        { 0x00000000ffffffffULL, 0x00000000ffffffffULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x00000000aaaaaaaaULL, 0x00000000aaaaaaaaULL, },
        { 0x0000000055555555ULL, 0x0000000055555555ULL, },
        { 0x00000000ccccccccULL, 0x00000000ccccccccULL, },
        { 0x0000000033333333ULL, 0x0000000033333333ULL, },
        { 0x00000000e38e38e3ULL, 0x0000000038e38e38ULL, },
        { 0x000000001c71c71cULL, 0x00000000c71c71c7ULL, },
        { 0xaaaaaaaaffffffffULL, 0xaaaaaaaaffffffffULL, },    /*  16  */
        { 0xaaaaaaaa00000000ULL, 0xaaaaaaaa00000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaa55555555ULL, 0xaaaaaaaa55555555ULL, },
        { 0xaaaaaaaaccccccccULL, 0xaaaaaaaaccccccccULL, },
        { 0xaaaaaaaa33333333ULL, 0xaaaaaaaa33333333ULL, },
        { 0xaaaaaaaae38e38e3ULL, 0xaaaaaaaa38e38e38ULL, },
        { 0xaaaaaaaa1c71c71cULL, 0xaaaaaaaac71c71c7ULL, },
        { 0x55555555ffffffffULL, 0x55555555ffffffffULL, },    /*  24  */
        { 0x5555555500000000ULL, 0x5555555500000000ULL, },
        { 0x55555555aaaaaaaaULL, 0x55555555aaaaaaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x55555555ccccccccULL, 0x55555555ccccccccULL, },
        { 0x5555555533333333ULL, 0x5555555533333333ULL, },
        { 0x55555555e38e38e3ULL, 0x5555555538e38e38ULL, },
        { 0x555555551c71c71cULL, 0x55555555c71c71c7ULL, },
        { 0xccccccccffffffffULL, 0xccccccccffffffffULL, },    /*  32  */
        { 0xcccccccc00000000ULL, 0xcccccccc00000000ULL, },
        { 0xccccccccaaaaaaaaULL, 0xccccccccaaaaaaaaULL, },
        { 0xcccccccc55555555ULL, 0xcccccccc55555555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xcccccccc33333333ULL, 0xcccccccc33333333ULL, },
        { 0xcccccccce38e38e3ULL, 0xcccccccc38e38e38ULL, },
        { 0xcccccccc1c71c71cULL, 0xccccccccc71c71c7ULL, },
        { 0x33333333ffffffffULL, 0x33333333ffffffffULL, },    /*  40  */
        { 0x3333333300000000ULL, 0x3333333300000000ULL, },
        { 0x33333333aaaaaaaaULL, 0x33333333aaaaaaaaULL, },
        { 0x3333333355555555ULL, 0x3333333355555555ULL, },
        { 0x33333333ccccccccULL, 0x33333333ccccccccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x33333333e38e38e3ULL, 0x3333333338e38e38ULL, },
        { 0x333333331c71c71cULL, 0x33333333c71c71c7ULL, },
        { 0xe38e38e3ffffffffULL, 0x38e38e38ffffffffULL, },    /*  48  */
        { 0xe38e38e300000000ULL, 0x38e38e3800000000ULL, },
        { 0xe38e38e3aaaaaaaaULL, 0x38e38e38aaaaaaaaULL, },
        { 0xe38e38e355555555ULL, 0x38e38e3855555555ULL, },
        { 0xe38e38e3ccccccccULL, 0x38e38e38ccccccccULL, },
        { 0xe38e38e333333333ULL, 0x38e38e3833333333ULL, },
        { 0xe38e38e3e38e38e3ULL, 0x38e38e3838e38e38ULL, },
        { 0xe38e38e31c71c71cULL, 0x38e38e38c71c71c7ULL, },
        { 0x1c71c71cffffffffULL, 0xc71c71c7ffffffffULL, },    /*  56  */
        { 0x1c71c71c00000000ULL, 0xc71c71c700000000ULL, },
        { 0x1c71c71caaaaaaaaULL, 0xc71c71c7aaaaaaaaULL, },
        { 0x1c71c71c55555555ULL, 0xc71c71c755555555ULL, },
        { 0x1c71c71cccccccccULL, 0xc71c71c7ccccccccULL, },
        { 0x1c71c71c33333333ULL, 0xc71c71c733333333ULL, },
        { 0x1c71c71ce38e38e3ULL, 0xc71c71c738e38e38ULL, },
        { 0x1c71c71c1c71c71cULL, 0xc71c71c7c71c71c7ULL, },
        { 0x886ae6cc886ae6ccULL, 0x4b670b5e4b670b5eULL, },    /*  64  */
        { 0x886ae6ccfbbe0063ULL, 0x4b670b5e12f7bb1aULL, },
        { 0x886ae6ccac5aaeaaULL, 0x4b670b5e27d8c6ffULL, },
        { 0x886ae6cc704f164dULL, 0x4b670b5e8df188d8ULL, },
        { 0xfbbe0063886ae6ccULL, 0x12f7bb1a4b670b5eULL, },
        { 0xfbbe0063fbbe0063ULL, 0x12f7bb1a12f7bb1aULL, },
        { 0xfbbe0063ac5aaeaaULL, 0x12f7bb1a27d8c6ffULL, },
        { 0xfbbe0063704f164dULL, 0x12f7bb1a8df188d8ULL, },
        { 0xac5aaeaa886ae6ccULL, 0x27d8c6ff4b670b5eULL, },    /*  72  */
        { 0xac5aaeaafbbe0063ULL, 0x27d8c6ff12f7bb1aULL, },
        { 0xac5aaeaaac5aaeaaULL, 0x27d8c6ff27d8c6ffULL, },
        { 0xac5aaeaa704f164dULL, 0x27d8c6ff8df188d8ULL, },
        { 0x704f164d886ae6ccULL, 0x8df188d84b670b5eULL, },
        { 0x704f164dfbbe0063ULL, 0x8df188d812f7bb1aULL, },
        { 0x704f164dac5aaeaaULL, 0x8df188d827d8c6ffULL, },
        { 0x704f164d704f164dULL, 0x8df188d88df188d8ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_ILVOD_W(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_ILVOD_W(b128_random[i], b128_random[j],
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
