/*
 *  Test program for MSA instruction MOD_U.H
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
    char *instruction_name =  "MOD_U.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x1c711c7371c71c71ULL, 0x1c7371c71c711c73ULL, },
        { 0x000638e31c710006ULL, 0x38e31c71000638e3ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x1111111111111111ULL, 0x1111111111111111ULL, },
        { 0xaaaa00011c72aaaaULL, 0x00011c72aaaa0001ULL, },
        { 0x0004aaaa38e30004ULL, 0xaaaa38e30004aaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x55551c7255555555ULL, 0x1c72555555551c72ULL, },
        { 0x0002555555550002ULL, 0x5555555500025555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xcccc22233e94ccccULL, 0x22233e94cccc2223ULL, },
        { 0x05b505b05b0505b5ULL, 0x05b05b0505b505b0ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x16c23333333316c2ULL, 0x3333333316c23333ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x38e438e38e3838e4ULL, 0x38e38e3838e438e3ULL, },
        { 0x38e438e338e338e4ULL, 0x38e338e338e438e3ULL, },
        { 0x16c238e38e3816c2ULL, 0x38e38e3816c238e3ULL, },
        { 0x16c205b027d216c2ULL, 0x05b027d216c205b0ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x000638e31c710006ULL, 0x38e31c71000638e3ULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c711c7271c71c71ULL, 0x1c7271c71c711c72ULL, },
        { 0x1c711c721c721c71ULL, 0x1c721c721c711c72ULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c712d830b611c71ULL, 0x2d830b611c712d83ULL, },
        { 0x1c711c7371c71c71ULL, 0x1c7371c71c711c73ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  64  */
        { 0x886a005028625540ULL, 0x12820b5e14c60a14ULL, },
        { 0x886a382228625540ULL, 0x238f0b5e53501bbcULL, },
        { 0x181b07ca28625540ULL, 0x4b670b5e5539b00cULL, },
        { 0x7354006325311c88ULL, 0x12f7053a153f52fcULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x4f6400634d933b88ULL, 0x12f7bb1a153f08d4ULL, },
        { 0x1b2000634d93c708ULL, 0x12f73242153f52fcULL, },
        { 0x23f0aeaa18473640ULL, 0x27d805c1ab2b2514ULL, },    /*  72  */
        { 0xac5a00411ea98b80ULL, 0x01ea0be501332514ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x3c0b128f5b9e8b80ULL, 0x27d83e2701e92514ULL, },
        { 0x704f164d0d6d37ceULL, 0x428a0070a9423294ULL, },
        { 0x704f0042109e1b46ULL, 0x093088d814893ca8ULL, },
        { 0x704f164d5e3156ceULL, 0x166988d8a9420428ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MOD_U_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MOD_U_H(b128_random[i], b128_random[j],
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
