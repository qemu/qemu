/*
 *  Test program for MSA instruction ADDV.H
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
    char *group_name = "Int Add";
    char *instruction_name =  "ADDV.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xfffefffefffefffeULL, 0xfffefffefffefffeULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xaaa9aaa9aaa9aaa9ULL, 0xaaa9aaa9aaa9aaa9ULL, },
        { 0x5554555455545554ULL, 0x5554555455545554ULL, },
        { 0xcccbcccbcccbcccbULL, 0xcccbcccbcccbcccbULL, },
        { 0x3332333233323332ULL, 0x3332333233323332ULL, },
        { 0xe38d38e28e37e38dULL, 0x38e28e37e38d38e2ULL, },
        { 0x1c70c71b71c61c70ULL, 0xc71b71c61c70c71bULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0xaaa9aaa9aaa9aaa9ULL, 0xaaa9aaa9aaa9aaa9ULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x5554555455545554ULL, 0x5554555455545554ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x7776777677767776ULL, 0x7776777677767776ULL, },
        { 0xddddddddddddddddULL, 0xddddddddddddddddULL, },
        { 0x8e38e38d38e28e38ULL, 0xe38d38e28e38e38dULL, },
        { 0xc71b71c61c71c71bULL, 0x71c61c71c71b71c6ULL, },
        { 0x5554555455545554ULL, 0x5554555455545554ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x2221222122212221ULL, 0x2221222122212221ULL, },
        { 0x8888888888888888ULL, 0x8888888888888888ULL, },
        { 0x38e38e38e38d38e3ULL, 0x8e38e38d38e38e38ULL, },
        { 0x71c61c71c71c71c6ULL, 0x1c71c71c71c61c71ULL, },
        { 0xcccbcccbcccbcccbULL, 0xcccbcccbcccbcccbULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x7776777677767776ULL, 0x7776777677767776ULL, },
        { 0x2221222122212221ULL, 0x2221222122212221ULL, },
        { 0x9998999899989998ULL, 0x9998999899989998ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xb05a05af5b04b05aULL, 0x05af5b04b05a05afULL, },
        { 0xe93d93e83e93e93dULL, 0x93e83e93e93d93e8ULL, },
        { 0x3332333233323332ULL, 0x3332333233323332ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xddddddddddddddddULL, 0xddddddddddddddddULL, },
        { 0x8888888888888888ULL, 0x8888888888888888ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x6666666666666666ULL, 0x6666666666666666ULL, },
        { 0x16c16c16c16b16c1ULL, 0x6c16c16b16c16c16ULL, },
        { 0x4fa4fa4fa4fa4fa4ULL, 0xfa4fa4fa4fa4fa4fULL, },
        { 0xe38d38e28e37e38dULL, 0x38e28e37e38d38e2ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x8e38e38d38e28e38ULL, 0xe38d38e28e38e38dULL, },
        { 0x38e38e38e38d38e3ULL, 0x8e38e38d38e38e38ULL, },
        { 0xb05a05af5b04b05aULL, 0x05af5b04b05a05afULL, },
        { 0x16c16c16c16b16c1ULL, 0x6c16c16b16c16c16ULL, },
        { 0xc71c71c61c70c71cULL, 0x71c61c70c71c71c6ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x1c70c71b71c61c70ULL, 0xc71b71c61c70c71bULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0xc71b71c61c71c71bULL, 0x71c61c71c71b71c6ULL, },
        { 0x71c61c71c71c71c6ULL, 0x1c71c71c71c61c71ULL, },
        { 0xe93d93e83e93e93dULL, 0x93e83e93e93d93e8ULL, },
        { 0x4fa4fa4fa4fa4fa4ULL, 0xfa4fa4fa4fa4fa4fULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x38e28e38e38e38e2ULL, 0x8e38e38e38e28e38ULL, },
        { 0x10d4cd9850c4aa80ULL, 0x96ce16bcfcf66018ULL, },    /*  64  */
        { 0x8428e72f75f51c48ULL, 0x5e5ec67813ba0308ULL, },
        { 0x34c49576e231e0c0ULL, 0x733fd25da9a6d520ULL, },
        { 0xf8b9fd198693378eULL, 0xd9589436a7bd92acULL, },
        { 0x8428e72f75f51c48ULL, 0x5e5ec67813ba0308ULL, },
        { 0xf77c00c69b268e10ULL, 0x25ee76342a7ea5f8ULL, },
        { 0xa818af0d07625288ULL, 0x3acf8219c06a7810ULL, },
        { 0x6c0d16b0abc4a956ULL, 0xa0e843f2be81359cULL, },
        { 0x34c49576e231e0c0ULL, 0x733fd25da9a6d520ULL, },    /*  72  */
        { 0xa818af0d07625288ULL, 0x3acf8219c06a7810ULL, },
        { 0x58b45d54739e1700ULL, 0x4fb08dfe56564a28ULL, },
        { 0x1ca9c4f718006dceULL, 0xb5c94fd7546d07b4ULL, },
        { 0xf8b9fd198693378eULL, 0xd9589436a7bd92acULL, },
        { 0x6c0d16b0abc4a956ULL, 0xa0e843f2be81359cULL, },
        { 0x1ca9c4f718006dceULL, 0xb5c94fd7546d07b4ULL, },
        { 0xe09e2c9abc62c49cULL, 0x1be211b05284c540ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_ADDV_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_ADDV_H(b128_random[i], b128_random[j],
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
