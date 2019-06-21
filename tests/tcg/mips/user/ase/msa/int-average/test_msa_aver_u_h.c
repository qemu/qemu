/*
 *  Test program for MSA instruction AVER_U.H
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
    char *instruction_name =  "AVER_U.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0x8000800080008000ULL, 0x8000800080008000ULL, },
        { 0xd555d555d555d555ULL, 0xd555d555d555d555ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xe666e666e666e666ULL, 0xe666e666e666e666ULL, },
        { 0x9999999999999999ULL, 0x9999999999999999ULL, },
        { 0xf1c79c71c71cf1c7ULL, 0x9c71c71cf1c79c71ULL, },
        { 0x8e38e38eb8e38e38ULL, 0xe38eb8e38e38e38eULL, },
        { 0x8000800080008000ULL, 0x8000800080008000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x2aab2aab2aab2aabULL, 0x2aab2aab2aab2aabULL, },
        { 0x6666666666666666ULL, 0x6666666666666666ULL, },
        { 0x199a199a199a199aULL, 0x199a199a199a199aULL, },
        { 0x71c71c72471c71c7ULL, 0x1c72471c71c71c72ULL, },
        { 0x0e39638e38e40e39ULL, 0x638e38e40e39638eULL, },
        { 0xd555d555d555d555ULL, 0xd555d555d555d555ULL, },    /*  16  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x8000800080008000ULL, 0x8000800080008000ULL, },
        { 0xbbbbbbbbbbbbbbbbULL, 0xbbbbbbbbbbbbbbbbULL, },
        { 0x6eef6eef6eef6eefULL, 0x6eef6eef6eef6eefULL, },
        { 0xc71c71c79c71c71cULL, 0x71c79c71c71c71c7ULL, },
        { 0x638eb8e38e39638eULL, 0xb8e38e39638eb8e3ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },    /*  24  */
        { 0x2aab2aab2aab2aabULL, 0x2aab2aab2aab2aabULL, },
        { 0x8000800080008000ULL, 0x8000800080008000ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x9111911191119111ULL, 0x9111911191119111ULL, },
        { 0x4444444444444444ULL, 0x4444444444444444ULL, },
        { 0x9c72471c71c79c72ULL, 0x471c71c79c72471cULL, },
        { 0x38e38e39638e38e3ULL, 0x8e39638e38e38e39ULL, },
        { 0xe666e666e666e666ULL, 0xe666e666e666e666ULL, },    /*  32  */
        { 0x6666666666666666ULL, 0x6666666666666666ULL, },
        { 0xbbbbbbbbbbbbbbbbULL, 0xbbbbbbbbbbbbbbbbULL, },
        { 0x9111911191119111ULL, 0x9111911191119111ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x8000800080008000ULL, 0x8000800080008000ULL, },
        { 0xd82d82d8ad82d82dULL, 0x82d8ad82d82d82d8ULL, },
        { 0x749fc9f49f4a749fULL, 0xc9f49f4a749fc9f4ULL, },
        { 0x9999999999999999ULL, 0x9999999999999999ULL, },    /*  40  */
        { 0x199a199a199a199aULL, 0x199a199a199a199aULL, },
        { 0x6eef6eef6eef6eefULL, 0x6eef6eef6eef6eefULL, },
        { 0x4444444444444444ULL, 0x4444444444444444ULL, },
        { 0x8000800080008000ULL, 0x8000800080008000ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x8b61360b60b68b61ULL, 0x360b60b68b61360bULL, },
        { 0x27d27d28527d27d2ULL, 0x7d28527d27d27d28ULL, },
        { 0xf1c79c71c71cf1c7ULL, 0x9c71c71cf1c79c71ULL, },    /*  48  */
        { 0x71c71c72471c71c7ULL, 0x1c72471c71c71c72ULL, },
        { 0xc71c71c79c71c71cULL, 0x71c79c71c71c71c7ULL, },
        { 0x9c72471c71c79c72ULL, 0x471c71c79c72471cULL, },
        { 0xd82d82d8ad82d82dULL, 0x82d8ad82d82d82d8ULL, },
        { 0x8b61360b60b68b61ULL, 0x360b60b68b61360bULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x8000800080008000ULL, 0x8000800080008000ULL, },
        { 0x8e38e38eb8e38e38ULL, 0xe38eb8e38e38e38eULL, },    /*  56  */
        { 0x0e39638e38e40e39ULL, 0x638e38e40e39638eULL, },
        { 0x638eb8e38e39638eULL, 0xb8e38e39638eb8e3ULL, },
        { 0x38e38e39638e38e3ULL, 0x8e39638e38e38e39ULL, },
        { 0x749fc9f49f4a749fULL, 0xc9f49f4a749fc9f4ULL, },
        { 0x27d27d28527d27d2ULL, 0x7d28527d27d27d28ULL, },
        { 0x8000800080008000ULL, 0x8000800080008000ULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x886ae6cc28625540ULL, 0x4b670b5efe7bb00cULL, },    /*  64  */
        { 0xc21473983afb8e24ULL, 0x2f2f633c89dd8184ULL, },
        { 0x9a62cabb71197060ULL, 0x39a0692fd4d36a90ULL, },
        { 0x7c5d7e8d434a9bc7ULL, 0x6cac4a1bd3dfc956ULL, },
        { 0xc21473983afb8e24ULL, 0x2f2f633c89dd8184ULL, },
        { 0xfbbe00634d93c708ULL, 0x12f7bb1a153f52fcULL, },
        { 0xd40c578783b1a944ULL, 0x1d68c10d60353c08ULL, },
        { 0xb6070b5855e2d4abULL, 0x5074a1f95f419aceULL, },
        { 0x9a62cabb71197060ULL, 0x39a0692fd4d36a90ULL, },    /*  72  */
        { 0xd40c578783b1a944ULL, 0x1d68c10d60353c08ULL, },
        { 0xac5aaeaab9cf8b80ULL, 0x27d8c6ffab2b2514ULL, },
        { 0x8e55627c8c00b6e7ULL, 0x5ae5a7ecaa3783daULL, },
        { 0x7c5d7e8d434a9bc7ULL, 0x6cac4a1bd3dfc956ULL, },
        { 0xb6070b5855e2d4abULL, 0x5074a1f95f419aceULL, },
        { 0x8e55627c8c00b6e7ULL, 0x5ae5a7ecaa3783daULL, },
        { 0x704f164d5e31e24eULL, 0x8df188d8a942e2a0ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_AVER_U_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_AVER_U_H(b128_random[i], b128_random[j],
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
