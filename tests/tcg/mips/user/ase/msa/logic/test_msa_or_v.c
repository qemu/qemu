/*
 *  Test program for MSA instruction OR.V
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
    char *instruction_name =  "OR.V";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xeeeeeeeeeeeeeeeeULL, 0xeeeeeeeeeeeeeeeeULL, },
        { 0xbbbbbbbbbbbbbbbbULL, 0xbbbbbbbbbbbbbbbbULL, },
        { 0xebaebaebaebaebaeULL, 0xbaebaebaebaebaebULL, },
        { 0xbefbefbefbefbefbULL, 0xefbefbefbefbefbeULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xddddddddddddddddULL, 0xddddddddddddddddULL, },
        { 0x7777777777777777ULL, 0x7777777777777777ULL, },
        { 0xf7df7df7df7df7dfULL, 0x7df7df7df7df7df7ULL, },
        { 0x5d75d75d75d75d75ULL, 0xd75d75d75d75d75dULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xeeeeeeeeeeeeeeeeULL, 0xeeeeeeeeeeeeeeeeULL, },
        { 0xddddddddddddddddULL, 0xddddddddddddddddULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xefcefcefcefcefceULL, 0xfcefcefcefcefcefULL, },
        { 0xdcfdcfdcfdcfdcfdULL, 0xcfdcfdcfdcfdcfdcULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xbbbbbbbbbbbbbbbbULL, 0xbbbbbbbbbbbbbbbbULL, },
        { 0x7777777777777777ULL, 0x7777777777777777ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xf3bf3bf3bf3bf3bfULL, 0x3bf3bf3bf3bf3bf3ULL, },
        { 0x3f73f73f73f73f73ULL, 0xf73f73f73f73f73fULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xebaebaebaebaebaeULL, 0xbaebaebaebaebaebULL, },
        { 0xf7df7df7df7df7dfULL, 0x7df7df7df7df7df7ULL, },
        { 0xefcefcefcefcefceULL, 0xfcefcefcefcefcefULL, },
        { 0xf3bf3bf3bf3bf3bfULL, 0x3bf3bf3bf3bf3bf3ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0xbefbefbefbefbefbULL, 0xefbefbefbefbefbeULL, },
        { 0x5d75d75d75d75d75ULL, 0xd75d75d75d75d75dULL, },
        { 0xdcfdcfdcfdcfdcfdULL, 0xcfdcfdcfdcfdcfdcULL, },
        { 0x3f73f73f73f73f73ULL, 0xf73f73f73f73f73fULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x886ae6cc28625540ULL, 0x4b670b5efe7bb00cULL, },    /*  64  */
        { 0xfbfee6ef6df3d748ULL, 0x5bf7bb5eff7ff2fcULL, },
        { 0xac7aeeeeb9efdfc0ULL, 0x6fffcfffff7bb51cULL, },
        { 0xf86ff6cd7e73f74eULL, 0xcff78bdeff7bf2acULL, },
        { 0xfbfee6ef6df3d748ULL, 0x5bf7bb5eff7ff2fcULL, },
        { 0xfbbe00634d93c708ULL, 0x12f7bb1a153f52fcULL, },
        { 0xfffeaeebfddfcf88ULL, 0x37ffffffbf3f77fcULL, },
        { 0xfbff166f5fb3e74eULL, 0x9ff7bbdabd7ff2fcULL, },
        { 0xac7aeeeeb9efdfc0ULL, 0x6fffcfffff7bb51cULL, },    /*  72  */
        { 0xfffeaeebfddfcf88ULL, 0x37ffffffbf3f77fcULL, },
        { 0xac5aaeaab9cf8b80ULL, 0x27d8c6ffab2b2514ULL, },
        { 0xfc5fbeefffffebceULL, 0xaff9ceffab6be7b4ULL, },
        { 0xf86ff6cd7e73f74eULL, 0xcff78bdeff7bf2acULL, },
        { 0xfbff166f5fb3e74eULL, 0x9ff7bbdabd7ff2fcULL, },
        { 0xfc5fbeefffffebceULL, 0xaff9ceffab6be7b4ULL, },
        { 0x704f164d5e31e24eULL, 0x8df188d8a942e2a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_OR_V(b128_pattern[i], b128_pattern[j],
                        b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_OR_V(b128_random[i], b128_random[j],
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
