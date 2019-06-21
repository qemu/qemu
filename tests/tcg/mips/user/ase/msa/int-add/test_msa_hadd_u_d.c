/*
 *  Test program for MSA instruction HADD_U.D
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
    char *instruction_name =  "HADD_U.D";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x00000001fffffffeULL, 0x00000001fffffffeULL, },    /*   0  */
        { 0x00000000ffffffffULL, 0x00000000ffffffffULL, },
        { 0x00000001aaaaaaa9ULL, 0x00000001aaaaaaa9ULL, },
        { 0x0000000155555554ULL, 0x0000000155555554ULL, },
        { 0x00000001cccccccbULL, 0x00000001cccccccbULL, },
        { 0x0000000133333332ULL, 0x0000000133333332ULL, },
        { 0x000000018e38e38dULL, 0x00000001e38e38e2ULL, },
        { 0x0000000171c71c70ULL, 0x000000011c71c71bULL, },
        { 0x00000000ffffffffULL, 0x00000000ffffffffULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x00000000aaaaaaaaULL, 0x00000000aaaaaaaaULL, },
        { 0x0000000055555555ULL, 0x0000000055555555ULL, },
        { 0x00000000ccccccccULL, 0x00000000ccccccccULL, },
        { 0x0000000033333333ULL, 0x0000000033333333ULL, },
        { 0x000000008e38e38eULL, 0x00000000e38e38e3ULL, },
        { 0x0000000071c71c71ULL, 0x000000001c71c71cULL, },
        { 0x00000001aaaaaaa9ULL, 0x00000001aaaaaaa9ULL, },    /*  16  */
        { 0x00000000aaaaaaaaULL, 0x00000000aaaaaaaaULL, },
        { 0x0000000155555554ULL, 0x0000000155555554ULL, },
        { 0x00000000ffffffffULL, 0x00000000ffffffffULL, },
        { 0x0000000177777776ULL, 0x0000000177777776ULL, },
        { 0x00000000ddddddddULL, 0x00000000ddddddddULL, },
        { 0x0000000138e38e38ULL, 0x000000018e38e38dULL, },
        { 0x000000011c71c71bULL, 0x00000000c71c71c6ULL, },
        { 0x0000000155555554ULL, 0x0000000155555554ULL, },    /*  24  */
        { 0x0000000055555555ULL, 0x0000000055555555ULL, },
        { 0x00000000ffffffffULL, 0x00000000ffffffffULL, },
        { 0x00000000aaaaaaaaULL, 0x00000000aaaaaaaaULL, },
        { 0x0000000122222221ULL, 0x0000000122222221ULL, },
        { 0x0000000088888888ULL, 0x0000000088888888ULL, },
        { 0x00000000e38e38e3ULL, 0x0000000138e38e38ULL, },
        { 0x00000000c71c71c6ULL, 0x0000000071c71c71ULL, },
        { 0x00000001cccccccbULL, 0x00000001cccccccbULL, },    /*  32  */
        { 0x00000000ccccccccULL, 0x00000000ccccccccULL, },
        { 0x0000000177777776ULL, 0x0000000177777776ULL, },
        { 0x0000000122222221ULL, 0x0000000122222221ULL, },
        { 0x0000000199999998ULL, 0x0000000199999998ULL, },
        { 0x00000000ffffffffULL, 0x00000000ffffffffULL, },
        { 0x000000015b05b05aULL, 0x00000001b05b05afULL, },
        { 0x000000013e93e93dULL, 0x00000000e93e93e8ULL, },
        { 0x0000000133333332ULL, 0x0000000133333332ULL, },    /*  40  */
        { 0x0000000033333333ULL, 0x0000000033333333ULL, },
        { 0x00000000ddddddddULL, 0x00000000ddddddddULL, },
        { 0x0000000088888888ULL, 0x0000000088888888ULL, },
        { 0x00000000ffffffffULL, 0x00000000ffffffffULL, },
        { 0x0000000066666666ULL, 0x0000000066666666ULL, },
        { 0x00000000c16c16c1ULL, 0x0000000116c16c16ULL, },
        { 0x00000000a4fa4fa4ULL, 0x000000004fa4fa4fULL, },
        { 0x00000001e38e38e2ULL, 0x0000000138e38e37ULL, },    /*  48  */
        { 0x00000000e38e38e3ULL, 0x0000000038e38e38ULL, },
        { 0x000000018e38e38dULL, 0x00000000e38e38e2ULL, },
        { 0x0000000138e38e38ULL, 0x000000008e38e38dULL, },
        { 0x00000001b05b05afULL, 0x0000000105b05b04ULL, },
        { 0x0000000116c16c16ULL, 0x000000006c16c16bULL, },
        { 0x0000000171c71c71ULL, 0x000000011c71c71bULL, },
        { 0x0000000155555554ULL, 0x0000000055555554ULL, },
        { 0x000000011c71c71bULL, 0x00000001c71c71c6ULL, },    /*  56  */
        { 0x000000001c71c71cULL, 0x00000000c71c71c7ULL, },
        { 0x00000000c71c71c6ULL, 0x0000000171c71c71ULL, },
        { 0x0000000071c71c71ULL, 0x000000011c71c71cULL, },
        { 0x00000000e93e93e8ULL, 0x0000000193e93e93ULL, },
        { 0x000000004fa4fa4fULL, 0x00000000fa4fa4faULL, },
        { 0x00000000aaaaaaaaULL, 0x00000001aaaaaaaaULL, },
        { 0x000000008e38e38dULL, 0x00000000e38e38e3ULL, },
        { 0x00000000b0cd3c0cULL, 0x0000000149e2bb6aULL, },    /*  64  */
        { 0x00000000d5feadd4ULL, 0x0000000060a65e5aULL, },
        { 0x00000001423a724cULL, 0x00000000f6923072ULL, },
        { 0x00000000e69cc91aULL, 0x00000000f4a9edfeULL, },
        { 0x00000001242055a3ULL, 0x0000000111736b26ULL, },
        { 0x000000014951c76bULL, 0x0000000028370e16ULL, },
        { 0x00000001b58d8be3ULL, 0x00000000be22e02eULL, },
        { 0x0000000159efe2b1ULL, 0x00000000bc3a9dbaULL, },
        { 0x00000000d4bd03eaULL, 0x000000012654770bULL, },    /*  72  */
        { 0x00000000f9ee75b2ULL, 0x000000003d1819fbULL, },
        { 0x00000001662a3a2aULL, 0x00000000d303ec13ULL, },
        { 0x000000010a8c90f8ULL, 0x00000000d11ba99fULL, },
        { 0x0000000098b16b8dULL, 0x000000018c6d38e4ULL, },
        { 0x00000000bde2dd55ULL, 0x00000000a330dbd4ULL, },
        { 0x000000012a1ea1cdULL, 0x00000001391cadecULL, },
        { 0x00000000ce80f89bULL, 0x0000000137346b78ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_HADD_U_D(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_HADD_U_D(b128_random[i], b128_random[j],
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
