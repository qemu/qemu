/*
 *  Test program for MSA instruction DOTP_U.W
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
    char *group_name = "Int Dot Product";
    char *instruction_name =  "DOTP_U.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xfffc0002fffc0002ULL, 0xfffc0002fffc0002ULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5552aaac5552aaacULL, 0x5552aaac5552aaacULL, },
        { 0xaaa95556aaa95556ULL, 0xaaa95556aaa95556ULL, },
        { 0x9996666899966668ULL, 0x9996666899966668ULL, },
        { 0x6665999a6665999aULL, 0x6665999a6665999aULL, },
        { 0x1c6fe38f71c48e3aULL, 0xc71a38e51c6fe38fULL, },
        { 0xe38c1c738e3771c8ULL, 0x38e1c71de38c1c73ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5552aaac5552aaacULL, 0x5552aaac5552aaacULL, },    /*  16  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xe38c71c8e38c71c8ULL, 0xe38c71c8e38c71c8ULL, },
        { 0x71c638e471c638e4ULL, 0x71c638e471c638e4ULL, },
        { 0x110eeef0110eeef0ULL, 0x110eeef0110eeef0ULL, },
        { 0x4443bbbc4443bbbcULL, 0x4443bbbc4443bbbcULL, },
        { 0xbd9fed0af683097cULL, 0x84bc25eebd9fed0aULL, },
        { 0x97b2bda25ecfa130ULL, 0xd09684be97b2bda2ULL, },
        { 0xaaa95556aaa95556ULL, 0xaaa95556aaa95556ULL, },    /*  24  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x71c638e471c638e4ULL, 0x71c638e471c638e4ULL, },
        { 0x38e31c7238e31c72ULL, 0x38e31c7238e31c72ULL, },
        { 0x8887777888877778ULL, 0x8887777888877778ULL, },
        { 0x2221ddde2221dddeULL, 0x2221ddde2221dddeULL, },
        { 0x5ecff6857b4184beULL, 0x425e12f75ecff685ULL, },
        { 0x4bd95ed12f67d098ULL, 0x684b425f4bd95ed1ULL, },
        { 0x9996666899966668ULL, 0x9996666899966668ULL, },    /*  32  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x110eeef0110eeef0ULL, 0x110eeef0110eeef0ULL, },
        { 0x8887777888877778ULL, 0x8887777888877778ULL, },
        { 0x47ab852047ab8520ULL, 0x47ab852047ab8520ULL, },
        { 0x51eae14851eae148ULL, 0x51eae14851eae148ULL, },
        { 0xe38cb60c27d071c8ULL, 0x9f482d84e38cb60cULL, },
        { 0xb609b05c71c5f4a0ULL, 0xfa4e38e4b609b05cULL, },
        { 0x6665999a6665999aULL, 0x6665999a6665999aULL, },    /*  40  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x4443bbbc4443bbbcULL, 0x4443bbbc4443bbbcULL, },
        { 0x2221ddde2221dddeULL, 0x2221ddde2221dddeULL, },
        { 0x51eae14851eae148ULL, 0x51eae14851eae148ULL, },
        { 0x147ab852147ab852ULL, 0x147ab852147ab852ULL, },
        { 0x38e32d8349f41c72ULL, 0x27d20b6138e32d83ULL, },
        { 0x2d826c171c717d28ULL, 0x3e938e392d826c17ULL, },
        { 0x1c6fe38f71c48e3aULL, 0xc71a38e51c6fe38fULL, },    /*  48  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xbd9fed0af683097cULL, 0x84bc25eebd9fed0aULL, },
        { 0x5ecff6857b4184beULL, 0x425e12f75ecff685ULL, },
        { 0xe38cb60c27d071c8ULL, 0x9f482d84e38cb60cULL, },
        { 0x38e32d8349f41c72ULL, 0x27d20b6138e32d83ULL, },
        { 0xd6e93c0d19474f04ULL, 0x5ba64589d6e93c0dULL, },
        { 0x4586a782587d3f36ULL, 0x6b73f35c4586a782ULL, },
        { 0xe38c1c738e3771c8ULL, 0x38e1c71de38c1c73ULL, },    /*  56  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x97b2bda25ecfa130ULL, 0xd09684be97b2bda2ULL, },
        { 0x4bd95ed12f67d098ULL, 0x684b425f4bd95ed1ULL, },
        { 0xb609b05c71c5f4a0ULL, 0xfa4e38e4b609b05cULL, },
        { 0x2d826c171c717d28ULL, 0x3e938e392d826c17ULL, },
        { 0x4586a782587d3f36ULL, 0x6b73f35c4586a782ULL, },
        { 0x9e0574f135ba3292ULL, 0xcd6dd3c19e0574f1ULL, },
        { 0x18c3fe7422c25584ULL, 0x16b6b9f57608cfa9ULL, },    /*  64  */
        { 0x867e6d904e841446ULL, 0x0de4cfed4e2fdb15ULL, },
        { 0xf94f18bc4bc3d93eULL, 0x1492568ac3a66499ULL, },
        { 0x4ff36c125a383042ULL, 0x2fe23e4744196e36ULL, },
        { 0x867e6d904e841446ULL, 0x0de4cfed4e2fdb15ULL, },
        { 0xf78e474db23f32a9ULL, 0x8a26a8f51ca9cd91ULL, },
        { 0xa9bfb48aa4c2d0ddULL, 0x94641c4e1a398e45ULL, },
        { 0x6e796f69cc7c8793ULL, 0x6e879377578266beULL, },
        { 0xf94f18bc4bc3d93eULL, 0x1492568ac3a66499ULL, },    /*  72  */
        { 0xa9bfb48aa4c2d0ddULL, 0x94641c4e1a398e45ULL, },
        { 0xeb349888d2e11561ULL, 0xa0e2f84177d142c9ULL, },
        { 0x5ad3b4e8bfaf139fULL, 0x8076d98091fe5896ULL, },
        { 0x4ff36c125a383042ULL, 0x2fe23e4744196e36ULL, },
        { 0x6e796f69cc7c8793ULL, 0x6e879377578266beULL, },
        { 0x5ad3b4e8bfaf139fULL, 0x8076d98091fe5896ULL, },
        { 0x33368b8aeab5d525ULL, 0x97d9932138871904ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_DOTP_U_W(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DOTP_U_W(b128_random[i], b128_random[j],
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
