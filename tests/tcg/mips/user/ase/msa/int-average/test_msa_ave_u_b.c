/*
 *  Test program for MSA instruction AVE_U.B
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
    char *instruction_name =  "AVE_U.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0xd4d4d4d4d4d4d4d4ULL, 0xd4d4d4d4d4d4d4d4ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xe5e5e5e5e5e5e5e5ULL, 0xe5e5e5e5e5e5e5e5ULL, },
        { 0x9999999999999999ULL, 0x9999999999999999ULL, },
        { 0xf1c69bf1c69bf1c6ULL, 0x9bf1c69bf1c69bf1ULL, },
        { 0x8db8e38db8e38db8ULL, 0xe38db8e38db8e38dULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x2a2a2a2a2a2a2a2aULL, 0x2a2a2a2a2a2a2a2aULL, },
        { 0x6666666666666666ULL, 0x6666666666666666ULL, },
        { 0x1919191919191919ULL, 0x1919191919191919ULL, },
        { 0x71471c71471c7147ULL, 0x1c71471c71471c71ULL, },
        { 0x0e38630e38630e38ULL, 0x630e38630e38630eULL, },
        { 0xd4d4d4d4d4d4d4d4ULL, 0xd4d4d4d4d4d4d4d4ULL, },    /*  16  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0xbbbbbbbbbbbbbbbbULL, 0xbbbbbbbbbbbbbbbbULL, },
        { 0x6e6e6e6e6e6e6e6eULL, 0x6e6e6e6e6e6e6e6eULL, },
        { 0xc69c71c69c71c69cULL, 0x71c69c71c69c71c6ULL, },
        { 0x638db8638db8638dULL, 0xb8638db8638db863ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },    /*  24  */
        { 0x2a2a2a2a2a2a2a2aULL, 0x2a2a2a2a2a2a2a2aULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x9090909090909090ULL, 0x9090909090909090ULL, },
        { 0x4444444444444444ULL, 0x4444444444444444ULL, },
        { 0x9c71469c71469c71ULL, 0x469c71469c71469cULL, },
        { 0x38638e38638e3863ULL, 0x8e38638e38638e38ULL, },
        { 0xe5e5e5e5e5e5e5e5ULL, 0xe5e5e5e5e5e5e5e5ULL, },    /*  32  */
        { 0x6666666666666666ULL, 0x6666666666666666ULL, },
        { 0xbbbbbbbbbbbbbbbbULL, 0xbbbbbbbbbbbbbbbbULL, },
        { 0x9090909090909090ULL, 0x9090909090909090ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0xd7ad82d7ad82d7adULL, 0x82d7ad82d7ad82d7ULL, },
        { 0x749ec9749ec9749eULL, 0xc9749ec9749ec974ULL, },
        { 0x9999999999999999ULL, 0x9999999999999999ULL, },    /*  40  */
        { 0x1919191919191919ULL, 0x1919191919191919ULL, },
        { 0x6e6e6e6e6e6e6e6eULL, 0x6e6e6e6e6e6e6e6eULL, },
        { 0x4444444444444444ULL, 0x4444444444444444ULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x8b60358b60358b60ULL, 0x358b60358b60358bULL, },
        { 0x27527d27527d2752ULL, 0x7d27527d27527d27ULL, },
        { 0xf1c69bf1c69bf1c6ULL, 0x9bf1c69bf1c69bf1ULL, },    /*  48  */
        { 0x71471c71471c7147ULL, 0x1c71471c71471c71ULL, },
        { 0xc69c71c69c71c69cULL, 0x71c69c71c69c71c6ULL, },
        { 0x9c71469c71469c71ULL, 0x469c71469c71469cULL, },
        { 0xd7ad82d7ad82d7adULL, 0x82d7ad82d7ad82d7ULL, },
        { 0x8b60358b60358b60ULL, 0x358b60358b60358bULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x8db8e38db8e38db8ULL, 0xe38db8e38db8e38dULL, },    /*  56  */
        { 0x0e38630e38630e38ULL, 0x630e38630e38630eULL, },
        { 0x638db8638db8638dULL, 0xb8638db8638db863ULL, },
        { 0x38638e38638e3863ULL, 0x8e38638e38638e38ULL, },
        { 0x749ec9749ec9749eULL, 0xc9749ec9749ec974ULL, },
        { 0x27527d27527d2752ULL, 0x7d27527d27527d27ULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x886ae6cc28625540ULL, 0x4b670b5efe7bb00cULL, },    /*  64  */
        { 0xc19473973a7a8e24ULL, 0x2eaf633c895d8184ULL, },
        { 0x9a62cabb70987060ULL, 0x399f68aed4536a10ULL, },
        { 0x7c5c7e8c43499b47ULL, 0x6cac499bd35ec956ULL, },
        { 0xc19473973a7a8e24ULL, 0x2eaf633c895d8184ULL, },
        { 0xfbbe00634d93c708ULL, 0x12f7bb1a153f52fcULL, },
        { 0xd38c578683b1a944ULL, 0x1ce7c08c60353b88ULL, },
        { 0xb5860b585562d42bULL, 0x4ff4a1795f409aceULL, },
        { 0x9a62cabb70987060ULL, 0x399f68aed4536a10ULL, },    /*  72  */
        { 0xd38c578683b1a944ULL, 0x1ce7c08c60353b88ULL, },
        { 0xac5aaeaab9cf8b80ULL, 0x27d8c6ffab2b2514ULL, },
        { 0x8e54627b8b80b667ULL, 0x5ae4a7ebaa36835aULL, },
        { 0x7c5c7e8c43499b47ULL, 0x6cac499bd35ec956ULL, },
        { 0xb5860b585562d42bULL, 0x4ff4a1795f409aceULL, },
        { 0x8e54627b8b80b667ULL, 0x5ae4a7ebaa36835aULL, },
        { 0x704f164d5e31e24eULL, 0x8df188d8a942e2a0ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_AVE_U_B(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_AVE_U_B(b128_random[i], b128_random[j],
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
