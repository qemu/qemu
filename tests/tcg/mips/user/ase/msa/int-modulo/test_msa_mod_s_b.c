/*
 *  Test program for MSA instruction MOD_S.B
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
    char *instruction_name =  "MOD_S.B";
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
        { 0xdedededededededeULL, 0xdedededededededeULL, },
        { 0xddddddddddddddddULL, 0xddddddddddddddddULL, },
        { 0xe4aae2e4aae2e4aaULL, 0xe2e4aae2e4aae2e4ULL, },
        { 0xfeaae3feaae3feaaULL, 0xe3feaae3feaae3feULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x2121212121212121ULL, 0x2121212121212121ULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x1b551d1b551d1b55ULL, 0x1d1b551d1b551d1bULL, },
        { 0x01551c01551c0155ULL, 0x1c01551c01551c01ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xe9cccce9cccce9ccULL, 0xcce9cccce9cccce9ULL, },
        { 0xe8cccce8cccce8ccULL, 0xcce8cccce8cccce8ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x1633331633331633ULL, 0x3316333316333316ULL, },
        { 0x1733331733331733ULL, 0x3317333317333317ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xe3e438e3e438e3e4ULL, 0x38e3e438e3e438e3ULL, },
        { 0xe3e338e3e338e3e3ULL, 0x38e3e338e3e338e3ULL, },
        { 0xe3f604e3f604e3f6ULL, 0x04e3f604e3f604e3ULL, },
        { 0xe3f405e3f405e3f4ULL, 0x05e3f405e3f405e3ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffff38ffff38ffffULL, 0x38ffff38ffff38ffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c1bc71c1bc71c1bULL, 0xc71c1bc71c1bc71cULL, },
        { 0x1c1cc71c1cc71c1cULL, 0xc71c1cc71c1cc71cULL, },
        { 0x1c09fb1c09fb1c09ULL, 0xfb1c09fb1c09fb1cULL, },
        { 0x1c0bfa1c0bfa1c0bULL, 0xfa1c0bfa1c0bfa1cULL, },
        { 0x1c71ff1c71ff1c71ULL, 0xff1c71ff1c71ff1cULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  64  */
        { 0x0028e6cc28621c00ULL, 0x03040b10fe3cb000ULL, },
        { 0xdc10e6cc28005540ULL, 0x24170b00fe25fa0cULL, },
        { 0xf81bfccc28001940ULL, 0x4b0d0b0efe39ec0cULL, },
        { 0xfbbe002f25f5c708ULL, 0x12f7fd1a013f02fcULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xfbbe000d06f5c708ULL, 0x12f7f500151408fcULL, },
        { 0xfbbe00164df5e508ULL, 0x12f7bb1a153f16fcULL, },
        { 0xac5afcdee1cfe000ULL, 0x27d8fdffff2b2508ULL, },    /*  72  */
        { 0xfc18aeaab9cffd00ULL, 0x03fcc6ffff2b2500ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xac0bf0f7b900e5ceULL, 0x27f6c6ffab2b0714ULL, },
        { 0x704f16190e31e20eULL, 0xd8f1f6d8ff42e200ULL, },
        { 0x020d164d1131e206ULL, 0xf9facdf2fd03e200ULL, },
        { 0x1c4f164d1700e24eULL, 0xdbf1fc00fe17e2f0ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MOD_S_B(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MOD_S_B(b128_random[i], b128_random[j],
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
