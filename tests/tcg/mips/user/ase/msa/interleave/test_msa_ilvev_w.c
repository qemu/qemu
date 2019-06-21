/*
 *  Test program for MSA instruction ILVEV.W
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
    char *instruction_name =  "ILVEV.W";
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
        { 0xffffffff8e38e38eULL, 0xffffffffe38e38e3ULL, },
        { 0xffffffff71c71c71ULL, 0xffffffff1c71c71cULL, },
        { 0x00000000ffffffffULL, 0x00000000ffffffffULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x00000000aaaaaaaaULL, 0x00000000aaaaaaaaULL, },
        { 0x0000000055555555ULL, 0x0000000055555555ULL, },
        { 0x00000000ccccccccULL, 0x00000000ccccccccULL, },
        { 0x0000000033333333ULL, 0x0000000033333333ULL, },
        { 0x000000008e38e38eULL, 0x00000000e38e38e3ULL, },
        { 0x0000000071c71c71ULL, 0x000000001c71c71cULL, },
        { 0xaaaaaaaaffffffffULL, 0xaaaaaaaaffffffffULL, },    /*  16  */
        { 0xaaaaaaaa00000000ULL, 0xaaaaaaaa00000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaa55555555ULL, 0xaaaaaaaa55555555ULL, },
        { 0xaaaaaaaaccccccccULL, 0xaaaaaaaaccccccccULL, },
        { 0xaaaaaaaa33333333ULL, 0xaaaaaaaa33333333ULL, },
        { 0xaaaaaaaa8e38e38eULL, 0xaaaaaaaae38e38e3ULL, },
        { 0xaaaaaaaa71c71c71ULL, 0xaaaaaaaa1c71c71cULL, },
        { 0x55555555ffffffffULL, 0x55555555ffffffffULL, },    /*  24  */
        { 0x5555555500000000ULL, 0x5555555500000000ULL, },
        { 0x55555555aaaaaaaaULL, 0x55555555aaaaaaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x55555555ccccccccULL, 0x55555555ccccccccULL, },
        { 0x5555555533333333ULL, 0x5555555533333333ULL, },
        { 0x555555558e38e38eULL, 0x55555555e38e38e3ULL, },
        { 0x5555555571c71c71ULL, 0x555555551c71c71cULL, },
        { 0xccccccccffffffffULL, 0xccccccccffffffffULL, },    /*  32  */
        { 0xcccccccc00000000ULL, 0xcccccccc00000000ULL, },
        { 0xccccccccaaaaaaaaULL, 0xccccccccaaaaaaaaULL, },
        { 0xcccccccc55555555ULL, 0xcccccccc55555555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xcccccccc33333333ULL, 0xcccccccc33333333ULL, },
        { 0xcccccccc8e38e38eULL, 0xcccccccce38e38e3ULL, },
        { 0xcccccccc71c71c71ULL, 0xcccccccc1c71c71cULL, },
        { 0x33333333ffffffffULL, 0x33333333ffffffffULL, },    /*  40  */
        { 0x3333333300000000ULL, 0x3333333300000000ULL, },
        { 0x33333333aaaaaaaaULL, 0x33333333aaaaaaaaULL, },
        { 0x3333333355555555ULL, 0x3333333355555555ULL, },
        { 0x33333333ccccccccULL, 0x33333333ccccccccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x333333338e38e38eULL, 0x33333333e38e38e3ULL, },
        { 0x3333333371c71c71ULL, 0x333333331c71c71cULL, },
        { 0x8e38e38effffffffULL, 0xe38e38e3ffffffffULL, },    /*  48  */
        { 0x8e38e38e00000000ULL, 0xe38e38e300000000ULL, },
        { 0x8e38e38eaaaaaaaaULL, 0xe38e38e3aaaaaaaaULL, },
        { 0x8e38e38e55555555ULL, 0xe38e38e355555555ULL, },
        { 0x8e38e38eccccccccULL, 0xe38e38e3ccccccccULL, },
        { 0x8e38e38e33333333ULL, 0xe38e38e333333333ULL, },
        { 0x8e38e38e8e38e38eULL, 0xe38e38e3e38e38e3ULL, },
        { 0x8e38e38e71c71c71ULL, 0xe38e38e31c71c71cULL, },
        { 0x71c71c71ffffffffULL, 0x1c71c71cffffffffULL, },    /*  56  */
        { 0x71c71c7100000000ULL, 0x1c71c71c00000000ULL, },
        { 0x71c71c71aaaaaaaaULL, 0x1c71c71caaaaaaaaULL, },
        { 0x71c71c7155555555ULL, 0x1c71c71c55555555ULL, },
        { 0x71c71c71ccccccccULL, 0x1c71c71cccccccccULL, },
        { 0x71c71c7133333333ULL, 0x1c71c71c33333333ULL, },
        { 0x71c71c718e38e38eULL, 0x1c71c71ce38e38e3ULL, },
        { 0x71c71c7171c71c71ULL, 0x1c71c71c1c71c71cULL, },
        { 0x2862554028625540ULL, 0xfe7bb00cfe7bb00cULL, },    /*  64  */
        { 0x286255404d93c708ULL, 0xfe7bb00c153f52fcULL, },
        { 0x28625540b9cf8b80ULL, 0xfe7bb00cab2b2514ULL, },
        { 0x286255405e31e24eULL, 0xfe7bb00ca942e2a0ULL, },
        { 0x4d93c70828625540ULL, 0x153f52fcfe7bb00cULL, },
        { 0x4d93c7084d93c708ULL, 0x153f52fc153f52fcULL, },
        { 0x4d93c708b9cf8b80ULL, 0x153f52fcab2b2514ULL, },
        { 0x4d93c7085e31e24eULL, 0x153f52fca942e2a0ULL, },
        { 0xb9cf8b8028625540ULL, 0xab2b2514fe7bb00cULL, },    /*  72  */
        { 0xb9cf8b804d93c708ULL, 0xab2b2514153f52fcULL, },
        { 0xb9cf8b80b9cf8b80ULL, 0xab2b2514ab2b2514ULL, },
        { 0xb9cf8b805e31e24eULL, 0xab2b2514a942e2a0ULL, },
        { 0x5e31e24e28625540ULL, 0xa942e2a0fe7bb00cULL, },
        { 0x5e31e24e4d93c708ULL, 0xa942e2a0153f52fcULL, },
        { 0x5e31e24eb9cf8b80ULL, 0xa942e2a0ab2b2514ULL, },
        { 0x5e31e24e5e31e24eULL, 0xa942e2a0a942e2a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_ILVEV_W(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_ILVEV_W(b128_random[i], b128_random[j],
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
