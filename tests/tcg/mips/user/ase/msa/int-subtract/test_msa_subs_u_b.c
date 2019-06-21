/*
 *  Test program for MSA instruction SUBS_U.B
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
    char *group_name = "Int Subtract";
    char *instruction_name =  "SUBS_U.B";
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
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x7777777777777777ULL, 0x7777777777777777ULL, },
        { 0x001c72001c72001cULL, 0x72001c72001c7200ULL, },
        { 0x8e39008e39008e39ULL, 0x008e39008e39008eULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x00001d00001d0000ULL, 0x1d00001d00001d00ULL, },
        { 0x3900003900003900ULL, 0x0039000039000039ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x7777777777777777ULL, 0x7777777777777777ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x9999999999999999ULL, 0x9999999999999999ULL, },
        { 0x003e94003e94003eULL, 0x94003e94003e9400ULL, },
        { 0xb05b05b05b05b05bULL, 0x05b05b05b05b05b0ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x1700001700001700ULL, 0x0017000017000017ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x3900003900003900ULL, 0x0039000039000039ULL, },
        { 0x8e39008e39008e39ULL, 0x008e39008e39008eULL, },
        { 0x1700001700001700ULL, 0x0017000017000017ULL, },
        { 0xb05b05b05b05b05bULL, 0x05b05b05b05b05b0ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xc71d00c71d00c71dULL, 0x00c71d00c71d00c7ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x00001d00001d0000ULL, 0x1d00001d00001d00ULL, },
        { 0x001c72001c72001cULL, 0x72001c72001c7200ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x003e94003e94003eULL, 0x94003e94003e9400ULL, },
        { 0x00008f00008f0000ULL, 0x8f00008f00008f00ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  64  */
        { 0x0000e66900000038ULL, 0x39000044e93c5e00ULL, },
        { 0x0010382200000000ULL, 0x2400000053508b00ULL, },
        { 0x181bd07f00310000ULL, 0x0000000055390000ULL, },
        { 0x7354000025317200ULL, 0x0090b000000000f0ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x4f64000000003c00ULL, 0x001f000000142de8ULL, },
        { 0x8b6f001600620000ULL, 0x000633000000005cULL, },
        { 0x24000000916d3640ULL, 0x0071bba100000008ULL, },    /*  72  */
        { 0x0000ae476c3c0078ULL, 0x15000be596000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x3c0b985d5b9e0032ULL, 0x00003e2702000000ULL, },
        { 0x0000000036008d0eULL, 0x428a7d7a00003294ULL, },
        { 0x0000160011001b46ULL, 0x7b0000be94039000ULL, },
        { 0x0000000000005700ULL, 0x661900000017bd8cULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_SUBS_U_B(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_SUBS_U_B(b128_random[i], b128_random[j],
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
