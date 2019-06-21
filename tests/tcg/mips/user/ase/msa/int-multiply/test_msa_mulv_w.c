/*
 *  Test program for MSA instruction MULV.W
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
    char *instruction_name =  "MULV.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5555555655555556ULL, 0x5555555655555556ULL, },
        { 0xaaaaaaabaaaaaaabULL, 0xaaaaaaabaaaaaaabULL, },
        { 0x3333333433333334ULL, 0x3333333433333334ULL, },
        { 0xcccccccdcccccccdULL, 0xcccccccdcccccccdULL, },
        { 0x1c71c71d71c71c72ULL, 0xc71c71c81c71c71dULL, },
        { 0xe38e38e48e38e38fULL, 0x38e38e39e38e38e4ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5555555655555556ULL, 0x5555555655555556ULL, },    /*  16  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xe38e38e4e38e38e4ULL, 0xe38e38e4e38e38e4ULL, },
        { 0x71c71c7271c71c72ULL, 0x71c71c7271c71c72ULL, },
        { 0x7777777877777778ULL, 0x7777777877777778ULL, },
        { 0xdddddddedddddddeULL, 0xdddddddedddddddeULL, },
        { 0x12f684bea12f684cULL, 0x84bda13012f684beULL, },
        { 0x425ed098b425ed0aULL, 0xd097b426425ed098ULL, },
        { 0xaaaaaaabaaaaaaabULL, 0xaaaaaaabaaaaaaabULL, },    /*  24  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x71c71c7271c71c72ULL, 0x71c71c7271c71c72ULL, },
        { 0x38e38e3938e38e39ULL, 0x38e38e3938e38e39ULL, },
        { 0xbbbbbbbcbbbbbbbcULL, 0xbbbbbbbcbbbbbbbcULL, },
        { 0xeeeeeeefeeeeeeefULL, 0xeeeeeeefeeeeeeefULL, },
        { 0x097b425fd097b426ULL, 0x425ed098097b425fULL, },
        { 0xa12f684cda12f685ULL, 0x684bda13a12f684cULL, },
        { 0x3333333433333334ULL, 0x3333333433333334ULL, },    /*  32  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x7777777877777778ULL, 0x7777777877777778ULL, },
        { 0xbbbbbbbcbbbbbbbcULL, 0xbbbbbbbcbbbbbbbcULL, },
        { 0x28f5c29028f5c290ULL, 0x28f5c29028f5c290ULL, },
        { 0x0a3d70a40a3d70a4ULL, 0x0a3d70a40a3d70a4ULL, },
        { 0xe38e38e427d27d28ULL, 0x9f49f4a0e38e38e4ULL, },
        { 0x4fa4fa500b60b60cULL, 0x93e93e944fa4fa50ULL, },
        { 0xcccccccdcccccccdULL, 0xcccccccdcccccccdULL, },    /*  40  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xdddddddedddddddeULL, 0xdddddddedddddddeULL, },
        { 0xeeeeeeefeeeeeeefULL, 0xeeeeeeefeeeeeeefULL, },
        { 0x0a3d70a40a3d70a4ULL, 0x0a3d70a40a3d70a4ULL, },
        { 0xc28f5c29c28f5c29ULL, 0xc28f5c29c28f5c29ULL, },
        { 0x38e38e3949f49f4aULL, 0x27d27d2838e38e39ULL, },
        { 0x93e93e9482d82d83ULL, 0xa4fa4fa593e93e94ULL, },
        { 0x1c71c71d71c71c72ULL, 0xc71c71c81c71c71dULL, },    /*  48  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x12f684bea12f684cULL, 0x84bda13012f684beULL, },
        { 0x097b425fd097b426ULL, 0x425ed098097b425fULL, },
        { 0xe38e38e427d27d28ULL, 0x9f49f4a0e38e38e4ULL, },
        { 0x38e38e3949f49f4aULL, 0x27d27d2838e38e39ULL, },
        { 0xba781949e06522c4ULL, 0x06522c40ba781949ULL, },
        { 0x61f9add49161f9aeULL, 0xc0ca458861f9add4ULL, },
        { 0xe38e38e48e38e38fULL, 0x38e38e39e38e38e4ULL, },    /*  56  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x425ed098b425ed0aULL, 0xd097b426425ed098ULL, },
        { 0xa12f684cda12f685ULL, 0x684bda13a12f684cULL, },
        { 0x4fa4fa500b60b60cULL, 0x93e93e944fa4fa50ULL, },
        { 0x93e93e9482d82d83ULL, 0xa4fa4fa593e93e94ULL, },
        { 0x61f9add49161f9aeULL, 0xc0ca458861f9add4ULL, },
        { 0x81948b10fcd6e9e1ULL, 0x781948b181948b10ULL, },
        { 0xb103329061639000ULL, 0x3a25368474988090ULL, },    /*  64  */
        { 0x10bf40e4e7176a00ULL, 0x8176d18c6f1923d0ULL, },
        { 0x7393eb78c4346000ULL, 0xb7bf06a2581f7cf0ULL, },
        { 0xb0f0f35cee787980ULL, 0xd67987508dd09f80ULL, },
        { 0x10bf40e4e7176a00ULL, 0x8176d18c6f1923d0ULL, },
        { 0xb4f42649fded7040ULL, 0x3ceafea44aee6810ULL, },
        { 0xf73d8bbebe6cdc00ULL, 0x53697ae61444e7b0ULL, },
        { 0x7abb9fc72143b470ULL, 0x11e5adf0efce5580ULL, },
        { 0x7393eb78c4346000ULL, 0xb7bf06a2581f7cf0ULL, },    /*  72  */
        { 0xf73d8bbebe6cdc00ULL, 0x53697ae61444e7b0ULL, },
        { 0xb6b388e4e5044000ULL, 0x1aff72013216c990ULL, },
        { 0xe8bf252289e38100ULL, 0x91ae5f28d4dad480ULL, },
        { 0xb0f0f35cee787980ULL, 0xd67987508dd09f80ULL, },
        { 0x7abb9fc72143b470ULL, 0x11e5adf0efce5580ULL, },
        { 0xe8bf252289e38100ULL, 0x91ae5f28d4dad480ULL, },
        { 0x25775329b1e9cfc4ULL, 0xdfd63640e31ee400ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MULV_W(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MULV_W(b128_random[i], b128_random[j],
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
