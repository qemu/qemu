/*
 *  Test program for MSA instruction AVER_S.W
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
    char *instruction_name =  "AVER_S.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xd5555555d5555555ULL, 0xd5555555d5555555ULL, },
        { 0x2aaaaaaa2aaaaaaaULL, 0x2aaaaaaa2aaaaaaaULL, },
        { 0xe6666666e6666666ULL, 0xe6666666e6666666ULL, },
        { 0x1999999919999999ULL, 0x1999999919999999ULL, },
        { 0xf1c71c71c71c71c7ULL, 0x1c71c71cf1c71c71ULL, },
        { 0x0e38e38e38e38e38ULL, 0xe38e38e30e38e38eULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xd5555555d5555555ULL, 0xd5555555d5555555ULL, },
        { 0x2aaaaaab2aaaaaabULL, 0x2aaaaaab2aaaaaabULL, },
        { 0xe6666666e6666666ULL, 0xe6666666e6666666ULL, },
        { 0x1999999a1999999aULL, 0x1999999a1999999aULL, },
        { 0xf1c71c72c71c71c7ULL, 0x1c71c71cf1c71c72ULL, },
        { 0x0e38e38e38e38e39ULL, 0xe38e38e40e38e38eULL, },
        { 0xd5555555d5555555ULL, 0xd5555555d5555555ULL, },    /*  16  */
        { 0xd5555555d5555555ULL, 0xd5555555d5555555ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xbbbbbbbbbbbbbbbbULL, 0xbbbbbbbbbbbbbbbbULL, },
        { 0xeeeeeeefeeeeeeefULL, 0xeeeeeeefeeeeeeefULL, },
        { 0xc71c71c79c71c71cULL, 0xf1c71c71c71c71c7ULL, },
        { 0xe38e38e30e38e38eULL, 0xb8e38e39e38e38e3ULL, },
        { 0x2aaaaaaa2aaaaaaaULL, 0x2aaaaaaa2aaaaaaaULL, },    /*  24  */
        { 0x2aaaaaab2aaaaaabULL, 0x2aaaaaab2aaaaaabULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x1111111111111111ULL, 0x1111111111111111ULL, },
        { 0x4444444444444444ULL, 0x4444444444444444ULL, },
        { 0x1c71c71cf1c71c72ULL, 0x471c71c71c71c71cULL, },
        { 0x38e38e39638e38e3ULL, 0x0e38e38e38e38e39ULL, },
        { 0xe6666666e6666666ULL, 0xe6666666e6666666ULL, },    /*  32  */
        { 0xe6666666e6666666ULL, 0xe6666666e6666666ULL, },
        { 0xbbbbbbbbbbbbbbbbULL, 0xbbbbbbbbbbbbbbbbULL, },
        { 0x1111111111111111ULL, 0x1111111111111111ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xd82d82d8ad82d82dULL, 0x02d82d82d82d82d8ULL, },
        { 0xf49f49f41f49f49fULL, 0xc9f49f4af49f49f4ULL, },
        { 0x1999999919999999ULL, 0x1999999919999999ULL, },    /*  40  */
        { 0x1999999a1999999aULL, 0x1999999a1999999aULL, },
        { 0xeeeeeeefeeeeeeefULL, 0xeeeeeeefeeeeeeefULL, },
        { 0x4444444444444444ULL, 0x4444444444444444ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x0b60b60be0b60b61ULL, 0x360b60b60b60b60bULL, },
        { 0x27d27d28527d27d2ULL, 0xfd27d27d27d27d28ULL, },
        { 0xf1c71c71c71c71c7ULL, 0x1c71c71cf1c71c71ULL, },    /*  48  */
        { 0xf1c71c72c71c71c7ULL, 0x1c71c71cf1c71c72ULL, },
        { 0xc71c71c79c71c71cULL, 0xf1c71c71c71c71c7ULL, },
        { 0x1c71c71cf1c71c72ULL, 0x471c71c71c71c71cULL, },
        { 0xd82d82d8ad82d82dULL, 0x02d82d82d82d82d8ULL, },
        { 0x0b60b60be0b60b61ULL, 0x360b60b60b60b60bULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0e38e38e38e38e38ULL, 0xe38e38e30e38e38eULL, },    /*  56  */
        { 0x0e38e38e38e38e39ULL, 0xe38e38e40e38e38eULL, },
        { 0xe38e38e30e38e38eULL, 0xb8e38e39e38e38e3ULL, },
        { 0x38e38e39638e38e3ULL, 0x0e38e38e38e38e39ULL, },
        { 0xf49f49f41f49f49fULL, 0xc9f49f4af49f49f4ULL, },
        { 0x27d27d28527d27d2ULL, 0xfd27d27d27d27d28ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x886ae6cc28625540ULL, 0x4b670b5efe7bb00cULL, },    /*  64  */
        { 0xc21473983afb0e24ULL, 0x2f2f633c09dd8184ULL, },
        { 0x9a62cabbf118f060ULL, 0x399fe92fd4d36a90ULL, },
        { 0xfc5cfe8d434a1bc7ULL, 0xecac4a1bd3df4956ULL, },
        { 0xc21473983afb0e24ULL, 0x2f2f633c09dd8184ULL, },
        { 0xfbbe00634d93c708ULL, 0x12f7bb1a153f52fcULL, },
        { 0xd40c578703b1a944ULL, 0x1d68410de0353c08ULL, },
        { 0x36068b5855e2d4abULL, 0xd074a1f9df411aceULL, },
        { 0x9a62cabbf118f060ULL, 0x399fe92fd4d36a90ULL, },    /*  72  */
        { 0xd40c578703b1a944ULL, 0x1d68410de0353c08ULL, },
        { 0xac5aaeaab9cf8b80ULL, 0x27d8c6ffab2b2514ULL, },
        { 0x0e54e27c0c00b6e7ULL, 0xdae527ecaa3703daULL, },
        { 0xfc5cfe8d434a1bc7ULL, 0xecac4a1bd3df4956ULL, },
        { 0x36068b5855e2d4abULL, 0xd074a1f9df411aceULL, },
        { 0x0e54e27c0c00b6e7ULL, 0xdae527ecaa3703daULL, },
        { 0x704f164d5e31e24eULL, 0x8df188d8a942e2a0ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_AVER_S_W(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_AVER_S_W(b128_random[i], b128_random[j],
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
