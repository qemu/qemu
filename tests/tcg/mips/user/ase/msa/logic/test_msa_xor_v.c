/*
 *  Test program for MSA instruction XOR.V
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
    char *group_name = "Logic";
    char *instruction_name =  "XOR.V";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x6666666666666666ULL, 0x6666666666666666ULL, },
        { 0x9999999999999999ULL, 0x9999999999999999ULL, },
        { 0x4924924924924924ULL, 0x9249249249249249ULL, },
        { 0xb6db6db6db6db6dbULL, 0x6db6db6db6db6db6ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x9999999999999999ULL, 0x9999999999999999ULL, },
        { 0x6666666666666666ULL, 0x6666666666666666ULL, },
        { 0xb6db6db6db6db6dbULL, 0x6db6db6db6db6db6ULL, },
        { 0x4924924924924924ULL, 0x9249249249249249ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x6666666666666666ULL, 0x6666666666666666ULL, },
        { 0x9999999999999999ULL, 0x9999999999999999ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x2f42f42f42f42f42ULL, 0xf42f42f42f42f42fULL, },
        { 0xd0bd0bd0bd0bd0bdULL, 0x0bd0bd0bd0bd0bd0ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x9999999999999999ULL, 0x9999999999999999ULL, },
        { 0x6666666666666666ULL, 0x6666666666666666ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xd0bd0bd0bd0bd0bdULL, 0x0bd0bd0bd0bd0bd0ULL, },
        { 0x2f42f42f42f42f42ULL, 0xf42f42f42f42f42fULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x4924924924924924ULL, 0x9249249249249249ULL, },
        { 0xb6db6db6db6db6dbULL, 0x6db6db6db6db6db6ULL, },
        { 0x2f42f42f42f42f42ULL, 0xf42f42f42f42f42fULL, },
        { 0xd0bd0bd0bd0bd0bdULL, 0x0bd0bd0bd0bd0bd0ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0xb6db6db6db6db6dbULL, 0x6db6db6db6db6db6ULL, },
        { 0x4924924924924924ULL, 0x9249249249249249ULL, },
        { 0xd0bd0bd0bd0bd0bdULL, 0x0bd0bd0bd0bd0bd0ULL, },
        { 0x2f42f42f42f42f42ULL, 0xf42f42f42f42f42fULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  64  */
        { 0x73d4e6af65f19248ULL, 0x5990b044eb44e2f0ULL, },
        { 0x2430486691addec0ULL, 0x6cbfcda155509518ULL, },
        { 0xf825f0817653b70eULL, 0xc6968386573952acULL, },
        { 0x73d4e6af65f19248ULL, 0x5990b044eb44e2f0ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x57e4aec9f45c4c88ULL, 0x352f7de5be1477e8ULL, },
        { 0x8bf1162e13a22546ULL, 0x9f0633c2bc7db05cULL, },
        { 0x2430486691addec0ULL, 0x6cbfcda155509518ULL, },    /*  72  */
        { 0x57e4aec9f45c4c88ULL, 0x352f7de5be1477e8ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xdc15b8e7e7fe69ceULL, 0xaa294e270269c7b4ULL, },
        { 0xf825f0817653b70eULL, 0xc6968386573952acULL, },
        { 0x8bf1162e13a22546ULL, 0x9f0633c2bc7db05cULL, },
        { 0xdc15b8e7e7fe69ceULL, 0xaa294e270269c7b4ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_XOR_V(b128_pattern[i], b128_pattern[j],
                         b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_XOR_V(b128_random[i], b128_random[j],
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
