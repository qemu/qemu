/*
 *  Test program for MSA instruction DOTP_S.D
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
    char *instruction_name =  "DOTP_S.D";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000002ULL, 0x0000000000000002ULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x00000000aaaaaaacULL, 0x00000000aaaaaaacULL, },
        { 0xffffffff55555556ULL, 0xffffffff55555556ULL, },
        { 0x0000000066666668ULL, 0x0000000066666668ULL, },
        { 0xffffffff9999999aULL, 0xffffffff9999999aULL, },
        { 0x000000008e38e38fULL, 0xffffffffe38e38e5ULL, },
        { 0xffffffff71c71c73ULL, 0x000000001c71c71dULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x00000000aaaaaaacULL, 0x00000000aaaaaaacULL, },    /*  16  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x38e38e39c71c71c8ULL, 0x38e38e39c71c71c8ULL, },
        { 0xc71c71c6e38e38e4ULL, 0xc71c71c6e38e38e4ULL, },
        { 0x22222222eeeeeef0ULL, 0x22222222eeeeeef0ULL, },
        { 0xddddddddbbbbbbbcULL, 0xddddddddbbbbbbbcULL, },
        { 0x2f684bdab425ed0aULL, 0xf684bda197b425eeULL, },
        { 0xd097b425f684bda2ULL, 0x097b425f12f684beULL, },
        { 0xffffffff55555556ULL, 0xffffffff55555556ULL, },    /*  24  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xc71c71c6e38e38e4ULL, 0xc71c71c6e38e38e4ULL, },
        { 0x38e38e3871c71c72ULL, 0x38e38e3871c71c72ULL, },
        { 0xdddddddd77777778ULL, 0xdddddddd77777778ULL, },
        { 0x22222221dddddddeULL, 0x22222221dddddddeULL, },
        { 0xd097b425da12f685ULL, 0x097b425e4bda12f7ULL, },
        { 0x2f684bd97b425ed1ULL, 0xf684bda1097b425fULL, },
        { 0x0000000066666668ULL, 0x0000000066666668ULL, },    /*  32  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x22222222eeeeeef0ULL, 0x22222222eeeeeef0ULL, },
        { 0xdddddddd77777778ULL, 0xdddddddd77777778ULL, },
        { 0x147ae14851eb8520ULL, 0x147ae14851eb8520ULL, },
        { 0xeb851eb8147ae148ULL, 0xeb851eb8147ae148ULL, },
        { 0x1c71c71d0b60b60cULL, 0xfa4fa4fa82d82d84ULL, },
        { 0xe38e38e35b05b05cULL, 0x05b05b05e38e38e4ULL, },
        { 0xffffffff9999999aULL, 0xffffffff9999999aULL, },    /*  40  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xddddddddbbbbbbbcULL, 0xddddddddbbbbbbbcULL, },
        { 0x22222221dddddddeULL, 0x22222221dddddddeULL, },
        { 0xeb851eb8147ae148ULL, 0xeb851eb8147ae148ULL, },
        { 0x147ae147851eb852ULL, 0x147ae147851eb852ULL, },
        { 0xe38e38e382d82d83ULL, 0x05b05b0560b60b61ULL, },
        { 0x1c71c71c16c16c17ULL, 0xfa4fa4fa38e38e39ULL, },
        { 0x000000008e38e38fULL, 0xffffffffe38e38e5ULL, },    /*  48  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x2f684bdab425ed0aULL, 0xf684bda197b425eeULL, },
        { 0xd097b425da12f685ULL, 0x097b425e4bda12f7ULL, },
        { 0x1c71c71d0b60b60cULL, 0xfa4fa4fa82d82d84ULL, },
        { 0xe38e38e382d82d83ULL, 0x05b05b0560b60b61ULL, },
        { 0x35ba78199add3c0dULL, 0x0fcd6e9dc0ca4589ULL, },
        { 0xca4587e6f35ba782ULL, 0xf032916222c3f35cULL, },
        { 0xffffffff71c71c73ULL, 0x000000001c71c71dULL, },    /*  56  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xd097b425f684bda2ULL, 0x097b425f12f684beULL, },
        { 0x2f684bd97b425ed1ULL, 0xf684bda1097b425fULL, },
        { 0xe38e38e35b05b05cULL, 0x05b05b05e38e38e4ULL, },
        { 0x1c71c71c16c16c17ULL, 0xfa4fa4fa38e38e39ULL, },
        { 0xca4587e6f35ba782ULL, 0xf032916222c3f35cULL, },
        { 0x35ba78187e6b74f1ULL, 0x0fcd6e9df9add3c1ULL, },
        { 0x3e3ad4ae1266c290ULL, 0x1637d725aebdb714ULL, },    /*  64  */
        { 0x0e3a0c27f7d6aae4ULL, 0x0575fbb7f08ff55cULL, },
        { 0x1c00082337c84b78ULL, 0x0c3d39640fde8392ULL, },
        { 0xda65cd5e9f696cdcULL, 0xdeeb6bec644a26d0ULL, },
        { 0x0e3a0c27f7d6aae4ULL, 0x0575fbb7f08ff55cULL, },
        { 0x17945c09b2e19689ULL, 0x032b395187d966b4ULL, },
        { 0xec1f0e54b5aa67beULL, 0xfbe95b6e67ae6296ULL, },
        { 0x1aad30609bff5437ULL, 0xf059a43d01b40370ULL, },
        { 0x1c00082337c84b78ULL, 0x0c3d39640fde8392ULL, },    /*  72  */
        { 0xec1f0e54b5aa67beULL, 0xfbe95b6e67ae6296ULL, },
        { 0x2e9326619bb7c8e4ULL, 0x225024d84d163b91ULL, },
        { 0xc17a5d0372a2a622ULL, 0x0afd6368668933a8ULL, },
        { 0xda65cd5e9f696cdcULL, 0xdeeb6bec644a26d0ULL, },
        { 0x1aad30609bff5437ULL, 0xf059a43d01b40370ULL, },
        { 0xc17a5d0372a2a622ULL, 0x0afd6368668933a8ULL, },
        { 0x53edf7dbd76122edULL, 0x50347e61c2f51a40ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_DOTP_S_D(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DOTP_S_D(b128_random[i], b128_random[j],
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
