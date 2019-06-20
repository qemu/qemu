/*
 *  Test program for MSA instruction MULV.D
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
    char *group_name = "Int Multiply";
    char *instruction_name =  "MULV.D";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000001ULL, 0x0000000000000001ULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5555555555555556ULL, 0x5555555555555556ULL, },
        { 0xaaaaaaaaaaaaaaabULL, 0xaaaaaaaaaaaaaaabULL, },
        { 0x3333333333333334ULL, 0x3333333333333334ULL, },
        { 0xcccccccccccccccdULL, 0xcccccccccccccccdULL, },
        { 0x1c71c71c71c71c72ULL, 0xc71c71c71c71c71dULL, },
        { 0xe38e38e38e38e38fULL, 0x38e38e38e38e38e4ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5555555555555556ULL, 0x5555555555555556ULL, },    /*  16  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x38e38e38e38e38e4ULL, 0x38e38e38e38e38e4ULL, },
        { 0x1c71c71c71c71c72ULL, 0x1c71c71c71c71c72ULL, },
        { 0x7777777777777778ULL, 0x7777777777777778ULL, },
        { 0xdddddddddddddddeULL, 0xdddddddddddddddeULL, },
        { 0x12f684bda12f684cULL, 0x2f684bda12f684beULL, },
        { 0x425ed097b425ed0aULL, 0x25ed097b425ed098ULL, },
        { 0xaaaaaaaaaaaaaaabULL, 0xaaaaaaaaaaaaaaabULL, },    /*  24  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x1c71c71c71c71c72ULL, 0x1c71c71c71c71c72ULL, },
        { 0x8e38e38e38e38e39ULL, 0x8e38e38e38e38e39ULL, },
        { 0xbbbbbbbbbbbbbbbcULL, 0xbbbbbbbbbbbbbbbcULL, },
        { 0xeeeeeeeeeeeeeeefULL, 0xeeeeeeeeeeeeeeefULL, },
        { 0x097b425ed097b426ULL, 0x97b425ed097b425fULL, },
        { 0xa12f684bda12f685ULL, 0x12f684bda12f684cULL, },
        { 0x3333333333333334ULL, 0x3333333333333334ULL, },    /*  32  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x7777777777777778ULL, 0x7777777777777778ULL, },
        { 0xbbbbbbbbbbbbbbbcULL, 0xbbbbbbbbbbbbbbbcULL, },
        { 0xf5c28f5c28f5c290ULL, 0xf5c28f5c28f5c290ULL, },
        { 0x3d70a3d70a3d70a4ULL, 0x3d70a3d70a3d70a4ULL, },
        { 0x7d27d27d27d27d28ULL, 0x38e38e38e38e38e4ULL, },
        { 0xb60b60b60b60b60cULL, 0xfa4fa4fa4fa4fa50ULL, },
        { 0xcccccccccccccccdULL, 0xcccccccccccccccdULL, },    /*  40  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xdddddddddddddddeULL, 0xdddddddddddddddeULL, },
        { 0xeeeeeeeeeeeeeeefULL, 0xeeeeeeeeeeeeeeefULL, },
        { 0x3d70a3d70a3d70a4ULL, 0x3d70a3d70a3d70a4ULL, },
        { 0x8f5c28f5c28f5c29ULL, 0x8f5c28f5c28f5c29ULL, },
        { 0x9f49f49f49f49f4aULL, 0x8e38e38e38e38e39ULL, },
        { 0x2d82d82d82d82d83ULL, 0x3e93e93e93e93e94ULL, },
        { 0x1c71c71c71c71c72ULL, 0xc71c71c71c71c71dULL, },    /*  48  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x12f684bda12f684cULL, 0x2f684bda12f684beULL, },
        { 0x097b425ed097b426ULL, 0x97b425ed097b425fULL, },
        { 0x7d27d27d27d27d28ULL, 0x38e38e38e38e38e4ULL, },
        { 0x9f49f49f49f49f4aULL, 0x8e38e38e38e38e39ULL, },
        { 0xb0fcd6e9e06522c4ULL, 0x522c3f35ba781949ULL, },
        { 0x6b74f0329161f9aeULL, 0x74f0329161f9add4ULL, },
        { 0xe38e38e38e38e38fULL, 0x38e38e38e38e38e4ULL, },    /*  56  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x425ed097b425ed0aULL, 0x25ed097b425ed098ULL, },
        { 0xa12f684bda12f685ULL, 0x12f684bda12f684cULL, },
        { 0xb60b60b60b60b60cULL, 0xfa4fa4fa4fa4fa50ULL, },
        { 0x2d82d82d82d82d83ULL, 0x3e93e93e93e93e94ULL, },
        { 0x6b74f0329161f9aeULL, 0x74f0329161f9add4ULL, },
        { 0x781948b0fcd6e9e1ULL, 0xc3f35ba781948b10ULL, },
        { 0xad45be6961639000ULL, 0x3297fdea74988090ULL, },    /*  64  */
        { 0xefa7a5a0e7176a00ULL, 0xb8110a1f6f1923d0ULL, },
        { 0x08c6139fc4346000ULL, 0xab209f86581f7cf0ULL, },
        { 0xfbe1883aee787980ULL, 0x821d25438dd09f80ULL, },
        { 0xefa7a5a0e7176a00ULL, 0xb8110a1f6f1923d0ULL, },
        { 0x37ae2b38fded7040ULL, 0x682476774aee6810ULL, },
        { 0x6acb3d68be6cdc00ULL, 0xafdad2311444e7b0ULL, },
        { 0xedbf72842143b470ULL, 0x7f8223caefce5580ULL, },
        { 0x08c6139fc4346000ULL, 0xab209f86581f7cf0ULL, },    /*  72  */
        { 0x6acb3d68be6cdc00ULL, 0xafdad2311444e7b0ULL, },
        { 0x8624e5e1e5044000ULL, 0xd98178a63216c990ULL, },
        { 0x76a5ab8089e38100ULL, 0xa1019a60d4dad480ULL, },
        { 0xfbe1883aee787980ULL, 0x821d25438dd09f80ULL, },
        { 0xedbf72842143b470ULL, 0x7f8223caefce5580ULL, },
        { 0x76a5ab8089e38100ULL, 0xa1019a60d4dad480ULL, },
        { 0x4bb436d5b1e9cfc4ULL, 0x12d1ceb0e31ee400ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MULV_D(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MULV_D(b128_random[i], b128_random[j],
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
