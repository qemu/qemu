/*
 *  Test program for MSA instruction MOD_S.H
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
    char *group_name = "Int Modulo";
    char *instruction_name =  "MOD_S.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xdddedddedddedddeULL, 0xdddedddedddedddeULL, },
        { 0xddddddddddddddddULL, 0xddddddddddddddddULL, },
        { 0x0000e38daaaa0000ULL, 0xe38daaaa0000e38dULL, },
        { 0xfffde38eaaaafffdULL, 0xe38eaaaafffde38eULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x2221222122212221ULL, 0x2221222122212221ULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x1c711c7255551c71ULL, 0x1c7255551c711c72ULL, },
        { 0x00021c7155550002ULL, 0x1c71555500021c71ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xe93ecccccccce93eULL, 0xcccccccce93eccccULL, },
        { 0xe93dcccccccce93dULL, 0xcccccccce93dccccULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x16c13333333316c1ULL, 0x3333333316c13333ULL, },
        { 0x16c23333333316c2ULL, 0x3333333316c23333ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xe38e38e3e38ee38eULL, 0x38e3e38ee38e38e3ULL, },
        { 0xe38e38e3e38de38eULL, 0x38e3e38de38e38e3ULL, },
        { 0xe38e05aff4a0e38eULL, 0x05aff4a0e38e05afULL, },
        { 0xe38e05b0f49ee38eULL, 0x05b0f49ee38e05b0ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffff38e3ffffffffULL, 0x38e3ffffffff38e3ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c71c71c1c711c71ULL, 0xc71c1c711c71c71cULL, },
        { 0x1c71c71c1c721c71ULL, 0xc71c1c721c71c71cULL, },
        { 0x1c71fa500b5f1c71ULL, 0xfa500b5f1c71fa50ULL, },
        { 0x1c71fa4f0b611c71ULL, 0xfa4f0b611c71fa4fULL, },
        { 0x1c71ffff71c71c71ULL, 0xffff71c71c71ffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  64  */
        { 0xffa2ffef28621c48ULL, 0x12820b5efe7bb00cULL, },
        { 0xdc10e6cc28625540ULL, 0x238f0b5efe7bfa34ULL, },
        { 0xf8b9fd19286219dcULL, 0x4b670b5efe7beaccULL, },
        { 0xfbbe00632531c708ULL, 0x12f7ff4e017e0308ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xfbbe00630762c708ULL, 0x12f7f41b153f08d4ULL, },
        { 0xfbbe00634d93e4baULL, 0x12f7bb1a153f183cULL, },
        { 0xac5afa46e231e0c0ULL, 0x27d8ffd5febe2514ULL, },    /*  72  */
        { 0xfd40ffe0b9cffd70ULL, 0x01eac6ffeae82514ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xac5af191b9cfe496ULL, 0x27d8c6ffab2b07b4ULL, },
        { 0x704f164d0d6de24eULL, 0xd958fa84ffdfe2a0ULL, },
        { 0x019b0042109ee24eULL, 0xffbbcdbefe3ee2a0ULL, },
        { 0x1ca9164d1800e24eULL, 0xdda1fadafe17e2a0ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MOD_S_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MOD_S_H(b128_random[i], b128_random[j],
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
