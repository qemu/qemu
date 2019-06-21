/*
 *  Test program for MSA instruction DOTP_U.D
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
    char *instruction_name =  "DOTP_U.D";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xfffffffc00000002ULL, 0xfffffffc00000002ULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x55555552aaaaaaacULL, 0x55555552aaaaaaacULL, },
        { 0xaaaaaaa955555556ULL, 0xaaaaaaa955555556ULL, },
        { 0x9999999666666668ULL, 0x9999999666666668ULL, },
        { 0x666666659999999aULL, 0x666666659999999aULL, },
        { 0x71c71c6f8e38e38fULL, 0x1c71c719e38e38e5ULL, },
        { 0x8e38e38c71c71c73ULL, 0xe38e38e21c71c71dULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x55555552aaaaaaacULL, 0x55555552aaaaaaacULL, },    /*  16  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xe38e38e1c71c71c8ULL, 0xe38e38e1c71c71c8ULL, },
        { 0x71c71c70e38e38e4ULL, 0x71c71c70e38e38e4ULL, },
        { 0x1111110eeeeeeef0ULL, 0x1111110eeeeeeef0ULL, },
        { 0x44444443bbbbbbbcULL, 0x44444443bbbbbbbcULL, },
        { 0xf684bd9fb425ed0aULL, 0xbda12f6697b425eeULL, },
        { 0x5ed097b2f684bda2ULL, 0x97b425ec12f684beULL, },
        { 0xaaaaaaa955555556ULL, 0xaaaaaaa955555556ULL, },    /*  24  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x71c71c70e38e38e4ULL, 0x71c71c70e38e38e4ULL, },
        { 0x38e38e3871c71c72ULL, 0x38e38e3871c71c72ULL, },
        { 0x8888888777777778ULL, 0x8888888777777778ULL, },
        { 0x22222221dddddddeULL, 0x22222221dddddddeULL, },
        { 0x7b425ecfda12f685ULL, 0x5ed097b34bda12f7ULL, },
        { 0x2f684bd97b425ed1ULL, 0x4bda12f6097b425fULL, },
        { 0x9999999666666668ULL, 0x9999999666666668ULL, },    /*  32  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x1111110eeeeeeef0ULL, 0x1111110eeeeeeef0ULL, },
        { 0x8888888777777778ULL, 0x8888888777777778ULL, },
        { 0x47ae147851eb8520ULL, 0x47ae147851eb8520ULL, },
        { 0x51eb851e147ae148ULL, 0x51eb851e147ae148ULL, },
        { 0x27d27d260b60b60cULL, 0xe38e38e182d82d84ULL, },
        { 0x71c71c705b05b05cULL, 0xb60b60b4e38e38e4ULL, },
        { 0x666666659999999aULL, 0x666666659999999aULL, },    /*  40  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x44444443bbbbbbbcULL, 0x44444443bbbbbbbcULL, },
        { 0x22222221dddddddeULL, 0x22222221dddddddeULL, },
        { 0x51eb851e147ae148ULL, 0x51eb851e147ae148ULL, },
        { 0x147ae147851eb852ULL, 0x147ae147851eb852ULL, },
        { 0x49f49f4982d82d83ULL, 0x38e38e3860b60b61ULL, },
        { 0x1c71c71c16c16c17ULL, 0x2d82d82d38e38e39ULL, },
        { 0x71c71c6f8e38e38fULL, 0x1c71c719e38e38e5ULL, },    /*  48  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xf684bd9fb425ed0aULL, 0xbda12f6697b425eeULL, },
        { 0x7b425ecfda12f685ULL, 0x5ed097b34bda12f7ULL, },
        { 0x27d27d260b60b60cULL, 0xe38e38e182d82d84ULL, },
        { 0x49f49f4982d82d83ULL, 0x38e38e3860b60b61ULL, },
        { 0x1948b0fb9add3c0dULL, 0xd6e9e063c0ca4589ULL, },
        { 0x587e6b73f35ba782ULL, 0x4587e6b622c3f35cULL, },
        { 0x8e38e38c71c71c73ULL, 0xe38e38e21c71c71dULL, },    /*  56  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5ed097b2f684bda2ULL, 0x97b425ec12f684beULL, },
        { 0x2f684bd97b425ed1ULL, 0x4bda12f6097b425fULL, },
        { 0x71c71c705b05b05cULL, 0xb60b60b4e38e38e4ULL, },
        { 0x1c71c71c16c16c17ULL, 0x2d82d82d38e38e39ULL, },
        { 0x587e6b73f35ba782ULL, 0x4587e6b622c3f35cULL, },
        { 0x35ba78187e6b74f1ULL, 0x9e06522bf9add3c1ULL, },
        { 0x4f10a2461266c290ULL, 0x132f373daebdb714ULL, },    /*  64  */
        { 0x9262f356f7d6aae4ULL, 0x1ab54eb3f08ff55cULL, },
        { 0x7927f2d937c84b78ULL, 0xb5e40e840fde8392ULL, },
        { 0x4ab4e3ab9f696cdcULL, 0xd21109f6644a26d0ULL, },
        { 0x9262f356f7d6aae4ULL, 0x1ab54eb3f08ff55cULL, },
        { 0x0f105ccfb2e19689ULL, 0x032b395187d966b4ULL, },
        { 0xe1cb8469b5aa67beULL, 0x1128ae6a67ae6296ULL, },
        { 0x8afc46ad9bff5437ULL, 0x1890b25301b40370ULL, },
        { 0x7927f2d937c84b78ULL, 0xb5e40e840fde8392ULL, },    /*  72  */
        { 0xe1cb8469b5aa67beULL, 0x1128ae6a67ae6296ULL, },
        { 0xfae79ab59bb7c8e4ULL, 0x78a66f004d163b91ULL, },
        { 0x8ffb559e72a2a622ULL, 0x8744321b668933a8ULL, },
        { 0x4ab4e3ab9f696cdcULL, 0xd21109f6644a26d0ULL, },
        { 0x8afc46ad9bff5437ULL, 0x1890b25301b40370ULL, },
        { 0x8ffb559e72a2a622ULL, 0x8744321b668933a8ULL, },
        { 0x53edf7dbd76122edULL, 0xbe9d5551c2f51a40ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_DOTP_U_D(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DOTP_U_D(b128_random[i], b128_random[j],
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
