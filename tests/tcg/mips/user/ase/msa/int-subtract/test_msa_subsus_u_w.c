/*
 *  Test program for MSA instruction SUBSUS_U.W
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
    char *instruction_name =  "SUBSUS_U.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xffffffffffffffffULL, 0xc71c71c7ffffffffULL, },
        { 0xe38e38e38e38e38eULL, 0xffffffffe38e38e3ULL, },
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5555555655555556ULL, 0x5555555655555556ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x3333333433333334ULL, 0x3333333433333334ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x1c71c71d71c71c72ULL, 0x000000001c71c71dULL, },
        { 0x0000000000000000ULL, 0x38e38e3900000000ULL, },
        { 0xaaaaaaabaaaaaaabULL, 0xaaaaaaabaaaaaaabULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xdddddddedddddddeULL, 0xdddddddedddddddeULL, },
        { 0x7777777777777777ULL, 0x7777777777777777ULL, },
        { 0xc71c71c7ffffffffULL, 0x71c71c72c71c71c7ULL, },
        { 0x8e38e38e38e38e39ULL, 0xe38e38e38e38e38eULL, },
        { 0x5555555655555556ULL, 0x5555555655555556ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xaaaaaaabaaaaaaabULL, 0xaaaaaaabaaaaaaabULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x8888888988888889ULL, 0x8888888988888889ULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x71c71c72c71c71c7ULL, 0x1c71c71d71c71c72ULL, },
        { 0x38e38e3900000000ULL, 0x8e38e38e38e38e39ULL, },
        { 0xcccccccdcccccccdULL, 0xcccccccdcccccccdULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x7777777777777777ULL, 0x7777777777777777ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x9999999999999999ULL, 0x9999999999999999ULL, },
        { 0xe93e93e9ffffffffULL, 0x93e93e94e93e93e9ULL, },
        { 0xb05b05b05b05b05bULL, 0xffffffffb05b05b0ULL, },
        { 0x3333333433333334ULL, 0x3333333433333334ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x8888888988888889ULL, 0x8888888988888889ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x6666666766666667ULL, 0x6666666766666667ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x4fa4fa50a4fa4fa5ULL, 0x000000004fa4fa50ULL, },
        { 0x16c16c1700000000ULL, 0x6c16c16c16c16c17ULL, },
        { 0xe38e38e48e38e38fULL, 0x38e38e39e38e38e4ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xffffffffe38e38e4ULL, 0x8e38e38effffffffULL, },
        { 0x8e38e38e38e38e39ULL, 0x000000008e38e38eULL, },
        { 0xffffffffc16c16c2ULL, 0x6c16c16cffffffffULL, },
        { 0xb05b05b05b05b05bULL, 0x05b05b05b05b05b0ULL, },
        { 0xffffffffffffffffULL, 0x00000000ffffffffULL, },
        { 0xc71c71c71c71c71dULL, 0x71c71c71c71c71c7ULL, },
        { 0x1c71c71d71c71c72ULL, 0xc71c71c81c71c71dULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x71c71c72c71c71c7ULL, 0xffffffff71c71c72ULL, },
        { 0x000000001c71c71cULL, 0x71c71c7200000000ULL, },
        { 0x4fa4fa50a4fa4fa5ULL, 0xfa4fa4fb4fa4fa50ULL, },
        { 0x000000003e93e93eULL, 0x93e93e9400000000ULL, },
        { 0x38e38e39e38e38e3ULL, 0x8e38e38f38e38e39ULL, },
        { 0x0000000000000000ULL, 0xffffffff00000000ULL, },
        { 0xffffffff00000000ULL, 0x00000000ffffffffULL, },    /*  64  */
        { 0x8cace66900000000ULL, 0x386f5044e93c5d10ULL, },
        { 0xdc1038226e92c9c0ULL, 0x238e445fffffffffULL, },
        { 0x181bd07f00000000ULL, 0xbd758286ffffffffULL, },
        { 0xffffffff253171c8ULL, 0x0000000016c3a2f0ULL, },
        { 0xffffffff00000000ULL, 0x0000000000000000ULL, },
        { 0xffffffff93c43b88ULL, 0x000000006a142de8ULL, },
        { 0x8b6eea1600000000ULL, 0x850632426bfc705cULL, },
        { 0xffffffff916d3640ULL, 0x00000000acaf7508ULL, },    /*  72  */
        { 0xb09cae476c3bc478ULL, 0x14e10be595ebd218ULL, },
        { 0xffffffffffffffffULL, 0x00000000ffffffffULL, },
        { 0x3c0b985d5b9da932ULL, 0x99e73e27ffffffffULL, },
        { 0xe7e42f8135cf8d0eULL, 0x428a7d7aaac73294ULL, },
        { 0x749115ea109e1b46ULL, 0x7af9cdbe94038fa4ULL, },
        { 0xc3f467a3a46256ceULL, 0x6618c1d9fe17bd8cULL, },
        { 0x0000000000000000ULL, 0xffffffffffffffffULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_SUBSUS_U_W(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_SUBSUS_U_W(b128_random[i], b128_random[j],
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
