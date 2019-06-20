/*
 *  Test program for MSA instruction AVER_U.B
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
    char *instruction_name =  "AVER_U.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },
        { 0xd5d5d5d5d5d5d5d5ULL, 0xd5d5d5d5d5d5d5d5ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xe6e6e6e6e6e6e6e6ULL, 0xe6e6e6e6e6e6e6e6ULL, },
        { 0x9999999999999999ULL, 0x9999999999999999ULL, },
        { 0xf1c79cf1c79cf1c7ULL, 0x9cf1c79cf1c79cf1ULL, },
        { 0x8eb8e38eb8e38eb8ULL, 0xe38eb8e38eb8e38eULL, },
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x2b2b2b2b2b2b2b2bULL, 0x2b2b2b2b2b2b2b2bULL, },
        { 0x6666666666666666ULL, 0x6666666666666666ULL, },
        { 0x1a1a1a1a1a1a1a1aULL, 0x1a1a1a1a1a1a1a1aULL, },
        { 0x72471c72471c7247ULL, 0x1c72471c72471c72ULL, },
        { 0x0e39640e39640e39ULL, 0x640e39640e39640eULL, },
        { 0xd5d5d5d5d5d5d5d5ULL, 0xd5d5d5d5d5d5d5d5ULL, },    /*  16  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },
        { 0xbbbbbbbbbbbbbbbbULL, 0xbbbbbbbbbbbbbbbbULL, },
        { 0x6f6f6f6f6f6f6f6fULL, 0x6f6f6f6f6f6f6f6fULL, },
        { 0xc79c71c79c71c79cULL, 0x71c79c71c79c71c7ULL, },
        { 0x638eb9638eb9638eULL, 0xb9638eb9638eb963ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },    /*  24  */
        { 0x2b2b2b2b2b2b2b2bULL, 0x2b2b2b2b2b2b2b2bULL, },
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x9191919191919191ULL, 0x9191919191919191ULL, },
        { 0x4444444444444444ULL, 0x4444444444444444ULL, },
        { 0x9c72479c72479c72ULL, 0x479c72479c72479cULL, },
        { 0x39638e39638e3963ULL, 0x8e39638e39638e39ULL, },
        { 0xe6e6e6e6e6e6e6e6ULL, 0xe6e6e6e6e6e6e6e6ULL, },    /*  32  */
        { 0x6666666666666666ULL, 0x6666666666666666ULL, },
        { 0xbbbbbbbbbbbbbbbbULL, 0xbbbbbbbbbbbbbbbbULL, },
        { 0x9191919191919191ULL, 0x9191919191919191ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },
        { 0xd8ad82d8ad82d8adULL, 0x82d8ad82d8ad82d8ULL, },
        { 0x749fca749fca749fULL, 0xca749fca749fca74ULL, },
        { 0x9999999999999999ULL, 0x9999999999999999ULL, },    /*  40  */
        { 0x1a1a1a1a1a1a1a1aULL, 0x1a1a1a1a1a1a1a1aULL, },
        { 0x6f6f6f6f6f6f6f6fULL, 0x6f6f6f6f6f6f6f6fULL, },
        { 0x4444444444444444ULL, 0x4444444444444444ULL, },
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x8b61368b61368b61ULL, 0x368b61368b61368bULL, },
        { 0x28527d28527d2852ULL, 0x7d28527d28527d28ULL, },
        { 0xf1c79cf1c79cf1c7ULL, 0x9cf1c79cf1c79cf1ULL, },    /*  48  */
        { 0x72471c72471c7247ULL, 0x1c72471c72471c72ULL, },
        { 0xc79c71c79c71c79cULL, 0x71c79c71c79c71c7ULL, },
        { 0x9c72479c72479c72ULL, 0x479c72479c72479cULL, },
        { 0xd8ad82d8ad82d8adULL, 0x82d8ad82d8ad82d8ULL, },
        { 0x8b61368b61368b61ULL, 0x368b61368b61368bULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },
        { 0x8eb8e38eb8e38eb8ULL, 0xe38eb8e38eb8e38eULL, },    /*  56  */
        { 0x0e39640e39640e39ULL, 0x640e39640e39640eULL, },
        { 0x638eb9638eb9638eULL, 0xb9638eb9638eb963ULL, },
        { 0x39638e39638e3963ULL, 0x8e39638e39638e39ULL, },
        { 0x749fca749fca749fULL, 0xca749fca749fca74ULL, },
        { 0x28527d28527d2852ULL, 0x7d28527d28527d28ULL, },
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x886ae6cc28625540ULL, 0x4b670b5efe7bb00cULL, },    /*  64  */
        { 0xc29473983b7b8e24ULL, 0x2faf633c8a5d8184ULL, },
        { 0x9a62cabb71997060ULL, 0x39a069afd5536b10ULL, },
        { 0x7c5d7e8d434a9c47ULL, 0x6cac4a9bd45fc956ULL, },
        { 0xc29473983b7b8e24ULL, 0x2faf633c8a5d8184ULL, },
        { 0xfbbe00634d93c708ULL, 0x12f7bb1a153f52fcULL, },
        { 0xd48c578783b1a944ULL, 0x1de8c18d60353c88ULL, },
        { 0xb6870b585662d52bULL, 0x50f4a2795f419aceULL, },
        { 0x9a62cabb71997060ULL, 0x39a069afd5536b10ULL, },    /*  72  */
        { 0xd48c578783b1a944ULL, 0x1de8c18d60353c88ULL, },
        { 0xac5aaeaab9cf8b80ULL, 0x27d8c6ffab2b2514ULL, },
        { 0x8e55627c8c80b767ULL, 0x5ae5a7ecaa37845aULL, },
        { 0x7c5d7e8d434a9c47ULL, 0x6cac4a9bd45fc956ULL, },
        { 0xb6870b585662d52bULL, 0x50f4a2795f419aceULL, },
        { 0x8e55627c8c80b767ULL, 0x5ae5a7ecaa37845aULL, },
        { 0x704f164d5e31e24eULL, 0x8df188d8a942e2a0ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_AVER_U_B(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_AVER_U_B(b128_random[i], b128_random[j],
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
