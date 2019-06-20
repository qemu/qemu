/*
 *  Test program for MSA instruction AVER_S.B
 *
 *  Copyright (C) 2019  Wave Computing, Inc.
 *  Copyright (C) 2019  Aleksandar Markovic <amarkovic@wavecomp.com>
 *  Copyright (C) 2019  RT-RK Computer Based Systems LLC
 *  Copyright (C) 2019  Mateja Marjanovic <mateja.marjanovic@rt-rk.com>
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
    char *group_name = "Int Average";
    char *instruction_name =  "AVER_S.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xd5d5d5d5d5d5d5d5ULL, 0xd5d5d5d5d5d5d5d5ULL, },
        { 0x2a2a2a2a2a2a2a2aULL, 0x2a2a2a2a2a2a2a2aULL, },
        { 0xe6e6e6e6e6e6e6e6ULL, 0xe6e6e6e6e6e6e6e6ULL, },
        { 0x1919191919191919ULL, 0x1919191919191919ULL, },
        { 0xf1c71cf1c71cf1c7ULL, 0x1cf1c71cf1c71cf1ULL, },
        { 0x0e38e30e38e30e38ULL, 0xe30e38e30e38e30eULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xd5d5d5d5d5d5d5d5ULL, 0xd5d5d5d5d5d5d5d5ULL, },
        { 0x2b2b2b2b2b2b2b2bULL, 0x2b2b2b2b2b2b2b2bULL, },
        { 0xe6e6e6e6e6e6e6e6ULL, 0xe6e6e6e6e6e6e6e6ULL, },
        { 0x1a1a1a1a1a1a1a1aULL, 0x1a1a1a1a1a1a1a1aULL, },
        { 0xf2c71cf2c71cf2c7ULL, 0x1cf2c71cf2c71cf2ULL, },
        { 0x0e39e40e39e40e39ULL, 0xe40e39e40e39e40eULL, },
        { 0xd5d5d5d5d5d5d5d5ULL, 0xd5d5d5d5d5d5d5d5ULL, },    /*  16  */
        { 0xd5d5d5d5d5d5d5d5ULL, 0xd5d5d5d5d5d5d5d5ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xbbbbbbbbbbbbbbbbULL, 0xbbbbbbbbbbbbbbbbULL, },
        { 0xefefefefefefefefULL, 0xefefefefefefefefULL, },
        { 0xc79cf1c79cf1c79cULL, 0xf1c79cf1c79cf1c7ULL, },
        { 0xe30eb9e30eb9e30eULL, 0xb9e30eb9e30eb9e3ULL, },
        { 0x2a2a2a2a2a2a2a2aULL, 0x2a2a2a2a2a2a2a2aULL, },    /*  24  */
        { 0x2b2b2b2b2b2b2b2bULL, 0x2b2b2b2b2b2b2b2bULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x1111111111111111ULL, 0x1111111111111111ULL, },
        { 0x4444444444444444ULL, 0x4444444444444444ULL, },
        { 0x1cf2471cf2471cf2ULL, 0x471cf2471cf2471cULL, },
        { 0x39630e39630e3963ULL, 0x0e39630e39630e39ULL, },
        { 0xe6e6e6e6e6e6e6e6ULL, 0xe6e6e6e6e6e6e6e6ULL, },    /*  32  */
        { 0xe6e6e6e6e6e6e6e6ULL, 0xe6e6e6e6e6e6e6e6ULL, },
        { 0xbbbbbbbbbbbbbbbbULL, 0xbbbbbbbbbbbbbbbbULL, },
        { 0x1111111111111111ULL, 0x1111111111111111ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xd8ad02d8ad02d8adULL, 0x02d8ad02d8ad02d8ULL, },
        { 0xf41fcaf41fcaf41fULL, 0xcaf41fcaf41fcaf4ULL, },
        { 0x1919191919191919ULL, 0x1919191919191919ULL, },    /*  40  */
        { 0x1a1a1a1a1a1a1a1aULL, 0x1a1a1a1a1a1a1a1aULL, },
        { 0xefefefefefefefefULL, 0xefefefefefefefefULL, },
        { 0x4444444444444444ULL, 0x4444444444444444ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x0be1360be1360be1ULL, 0x360be1360be1360bULL, },
        { 0x2852fd2852fd2852ULL, 0xfd2852fd2852fd28ULL, },
        { 0xf1c71cf1c71cf1c7ULL, 0x1cf1c71cf1c71cf1ULL, },    /*  48  */
        { 0xf2c71cf2c71cf2c7ULL, 0x1cf2c71cf2c71cf2ULL, },
        { 0xc79cf1c79cf1c79cULL, 0xf1c79cf1c79cf1c7ULL, },
        { 0x1cf2471cf2471cf2ULL, 0x471cf2471cf2471cULL, },
        { 0xd8ad02d8ad02d8adULL, 0x02d8ad02d8ad02d8ULL, },
        { 0x0be1360be1360be1ULL, 0x360be1360be1360bULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0e38e30e38e30e38ULL, 0xe30e38e30e38e30eULL, },    /*  56  */
        { 0x0e39e40e39e40e39ULL, 0xe40e39e40e39e40eULL, },
        { 0xe30eb9e30eb9e30eULL, 0xb9e30eb9e30eb9e3ULL, },
        { 0x39630e39630e3963ULL, 0x0e39630e39630e39ULL, },
        { 0xf41fcaf41fcaf41fULL, 0xcaf41fcaf41fcaf4ULL, },
        { 0x2852fd2852fd2852ULL, 0xfd2852fd2852fd28ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x886ae6cc28625540ULL, 0x4b670b5efe7bb00cULL, },    /*  64  */
        { 0xc214f3183bfb0e24ULL, 0x2f2fe33c0a5d0104ULL, },
        { 0x9a62cabbf119f0e0ULL, 0x3920e92fd553eb10ULL, },
        { 0xfc5dfe0d434a1c47ULL, 0xec2cca1bd45fc9d6ULL, },
        { 0xc214f3183bfb0e24ULL, 0x2f2fe33c0a5d0104ULL, },
        { 0xfbbe00634d93c708ULL, 0x12f7bb1a153f52fcULL, },
        { 0xd40cd70703b1a9c4ULL, 0x1de8c10de0353c08ULL, },
        { 0x36070b5856e2d52bULL, 0xd0f4a2f9df411aceULL, },
        { 0x9a62cabbf119f0e0ULL, 0x3920e92fd553eb10ULL, },    /*  72  */
        { 0xd40cd70703b1a9c4ULL, 0x1de8c10de0353c08ULL, },
        { 0xac5aaeaab9cf8b80ULL, 0x27d8c6ffab2b2514ULL, },
        { 0x0e55e2fc0c00b7e7ULL, 0xdae5a7ecaa3704daULL, },
        { 0xfc5dfe0d434a1c47ULL, 0xec2cca1bd45fc9d6ULL, },
        { 0x36070b5856e2d52bULL, 0xd0f4a2f9df411aceULL, },
        { 0x0e55e2fc0c00b7e7ULL, 0xdae5a7ecaa3704daULL, },
        { 0x704f164d5e31e24eULL, 0x8df188d8a942e2a0ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_AVER_S_B(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_AVER_S_B(b128_random[i], b128_random[j],
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
