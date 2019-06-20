/*
 *  Test program for MSA instruction AVE_S.B
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
    char *instruction_name =  "AVE_S.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xd4d4d4d4d4d4d4d4ULL, 0xd4d4d4d4d4d4d4d4ULL, },
        { 0x2a2a2a2a2a2a2a2aULL, 0x2a2a2a2a2a2a2a2aULL, },
        { 0xe5e5e5e5e5e5e5e5ULL, 0xe5e5e5e5e5e5e5e5ULL, },
        { 0x1919191919191919ULL, 0x1919191919191919ULL, },
        { 0xf1c61bf1c61bf1c6ULL, 0x1bf1c61bf1c61bf1ULL, },
        { 0x0d38e30d38e30d38ULL, 0xe30d38e30d38e30dULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xd5d5d5d5d5d5d5d5ULL, 0xd5d5d5d5d5d5d5d5ULL, },
        { 0x2a2a2a2a2a2a2a2aULL, 0x2a2a2a2a2a2a2a2aULL, },
        { 0xe6e6e6e6e6e6e6e6ULL, 0xe6e6e6e6e6e6e6e6ULL, },
        { 0x1919191919191919ULL, 0x1919191919191919ULL, },
        { 0xf1c71cf1c71cf1c7ULL, 0x1cf1c71cf1c71cf1ULL, },
        { 0x0e38e30e38e30e38ULL, 0xe30e38e30e38e30eULL, },
        { 0xd4d4d4d4d4d4d4d4ULL, 0xd4d4d4d4d4d4d4d4ULL, },    /*  16  */
        { 0xd5d5d5d5d5d5d5d5ULL, 0xd5d5d5d5d5d5d5d5ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xbbbbbbbbbbbbbbbbULL, 0xbbbbbbbbbbbbbbbbULL, },
        { 0xeeeeeeeeeeeeeeeeULL, 0xeeeeeeeeeeeeeeeeULL, },
        { 0xc69cf1c69cf1c69cULL, 0xf1c69cf1c69cf1c6ULL, },
        { 0xe30db8e30db8e30dULL, 0xb8e30db8e30db8e3ULL, },
        { 0x2a2a2a2a2a2a2a2aULL, 0x2a2a2a2a2a2a2a2aULL, },    /*  24  */
        { 0x2a2a2a2a2a2a2a2aULL, 0x2a2a2a2a2a2a2a2aULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x1010101010101010ULL, 0x1010101010101010ULL, },
        { 0x4444444444444444ULL, 0x4444444444444444ULL, },
        { 0x1cf1461cf1461cf1ULL, 0x461cf1461cf1461cULL, },
        { 0x38630e38630e3863ULL, 0x0e38630e38630e38ULL, },
        { 0xe5e5e5e5e5e5e5e5ULL, 0xe5e5e5e5e5e5e5e5ULL, },    /*  32  */
        { 0xe6e6e6e6e6e6e6e6ULL, 0xe6e6e6e6e6e6e6e6ULL, },
        { 0xbbbbbbbbbbbbbbbbULL, 0xbbbbbbbbbbbbbbbbULL, },
        { 0x1010101010101010ULL, 0x1010101010101010ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xd7ad02d7ad02d7adULL, 0x02d7ad02d7ad02d7ULL, },
        { 0xf41ec9f41ec9f41eULL, 0xc9f41ec9f41ec9f4ULL, },
        { 0x1919191919191919ULL, 0x1919191919191919ULL, },    /*  40  */
        { 0x1919191919191919ULL, 0x1919191919191919ULL, },
        { 0xeeeeeeeeeeeeeeeeULL, 0xeeeeeeeeeeeeeeeeULL, },
        { 0x4444444444444444ULL, 0x4444444444444444ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x0be0350be0350be0ULL, 0x350be0350be0350bULL, },
        { 0x2752fd2752fd2752ULL, 0xfd2752fd2752fd27ULL, },
        { 0xf1c61bf1c61bf1c6ULL, 0x1bf1c61bf1c61bf1ULL, },    /*  48  */
        { 0xf1c71cf1c71cf1c7ULL, 0x1cf1c71cf1c71cf1ULL, },
        { 0xc69cf1c69cf1c69cULL, 0xf1c69cf1c69cf1c6ULL, },
        { 0x1cf1461cf1461cf1ULL, 0x461cf1461cf1461cULL, },
        { 0xd7ad02d7ad02d7adULL, 0x02d7ad02d7ad02d7ULL, },
        { 0x0be0350be0350be0ULL, 0x350be0350be0350bULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0d38e30d38e30d38ULL, 0xe30d38e30d38e30dULL, },    /*  56  */
        { 0x0e38e30e38e30e38ULL, 0xe30e38e30e38e30eULL, },
        { 0xe30db8e30db8e30dULL, 0xb8e30db8e30db8e3ULL, },
        { 0x38630e38630e3863ULL, 0x0e38630e38630e38ULL, },
        { 0xf41ec9f41ec9f41eULL, 0xc9f41ec9f41ec9f4ULL, },
        { 0x2752fd2752fd2752ULL, 0xfd2752fd2752fd27ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x886ae6cc28625540ULL, 0x4b670b5efe7bb00cULL, },    /*  64  */
        { 0xc114f3173afa0e24ULL, 0x2e2fe33c095d0104ULL, },
        { 0x9a62cabbf018f0e0ULL, 0x391fe82ed453ea10ULL, },
        { 0xfc5cfe0c43491b47ULL, 0xec2cc91bd35ec9d6ULL, },
        { 0xc114f3173afa0e24ULL, 0x2e2fe33c095d0104ULL, },
        { 0xfbbe00634d93c708ULL, 0x12f7bb1a153f52fcULL, },
        { 0xd30cd70603b1a9c4ULL, 0x1ce7c00ce0353b08ULL, },
        { 0x35060b5855e2d42bULL, 0xcff4a1f9df401aceULL, },
        { 0x9a62cabbf018f0e0ULL, 0x391fe82ed453ea10ULL, },    /*  72  */
        { 0xd30cd70603b1a9c4ULL, 0x1ce7c00ce0353b08ULL, },
        { 0xac5aaeaab9cf8b80ULL, 0x27d8c6ffab2b2514ULL, },
        { 0x0e54e2fb0b00b6e7ULL, 0xdae4a7ebaa3603daULL, },
        { 0xfc5cfe0c43491b47ULL, 0xec2cc91bd35ec9d6ULL, },
        { 0x35060b5855e2d42bULL, 0xcff4a1f9df401aceULL, },
        { 0x0e54e2fb0b00b6e7ULL, 0xdae4a7ebaa3603daULL, },
        { 0x704f164d5e31e24eULL, 0x8df188d8a942e2a0ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_AVE_S_B(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_AVE_S_B(b128_random[i], b128_random[j],
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
