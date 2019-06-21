/*
 *  Test program for MSA instruction HSUB_S.D
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
    char *instruction_name =  "HSUB_S.D";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000055555555ULL, 0x0000000055555555ULL, },
        { 0xffffffffaaaaaaaaULL, 0xffffffffaaaaaaaaULL, },
        { 0x0000000033333333ULL, 0x0000000033333333ULL, },
        { 0xffffffffccccccccULL, 0xffffffffccccccccULL, },
        { 0x0000000071c71c71ULL, 0x000000001c71c71cULL, },
        { 0xffffffff8e38e38eULL, 0xffffffffe38e38e3ULL, },
        { 0x0000000000000001ULL, 0x0000000000000001ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000055555556ULL, 0x0000000055555556ULL, },
        { 0xffffffffaaaaaaabULL, 0xffffffffaaaaaaabULL, },
        { 0x0000000033333334ULL, 0x0000000033333334ULL, },
        { 0xffffffffcccccccdULL, 0xffffffffcccccccdULL, },
        { 0x0000000071c71c72ULL, 0x000000001c71c71dULL, },
        { 0xffffffff8e38e38fULL, 0xffffffffe38e38e4ULL, },
        { 0xffffffffaaaaaaabULL, 0xffffffffaaaaaaabULL, },    /*  16  */
        { 0xffffffffaaaaaaaaULL, 0xffffffffaaaaaaaaULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffff55555555ULL, 0xffffffff55555555ULL, },
        { 0xffffffffdddddddeULL, 0xffffffffdddddddeULL, },
        { 0xffffffff77777777ULL, 0xffffffff77777777ULL, },
        { 0x000000001c71c71cULL, 0xffffffffc71c71c7ULL, },
        { 0xffffffff38e38e39ULL, 0xffffffff8e38e38eULL, },
        { 0x0000000055555556ULL, 0x0000000055555556ULL, },    /*  24  */
        { 0x0000000055555555ULL, 0x0000000055555555ULL, },
        { 0x00000000aaaaaaabULL, 0x00000000aaaaaaabULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000088888889ULL, 0x0000000088888889ULL, },
        { 0x0000000022222222ULL, 0x0000000022222222ULL, },
        { 0x00000000c71c71c7ULL, 0x0000000071c71c72ULL, },
        { 0xffffffffe38e38e4ULL, 0x0000000038e38e39ULL, },
        { 0xffffffffcccccccdULL, 0xffffffffcccccccdULL, },    /*  32  */
        { 0xffffffffccccccccULL, 0xffffffffccccccccULL, },
        { 0x0000000022222222ULL, 0x0000000022222222ULL, },
        { 0xffffffff77777777ULL, 0xffffffff77777777ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffff99999999ULL, 0xffffffff99999999ULL, },
        { 0x000000003e93e93eULL, 0xffffffffe93e93e9ULL, },
        { 0xffffffff5b05b05bULL, 0xffffffffb05b05b0ULL, },
        { 0x0000000033333334ULL, 0x0000000033333334ULL, },    /*  40  */
        { 0x0000000033333333ULL, 0x0000000033333333ULL, },
        { 0x0000000088888889ULL, 0x0000000088888889ULL, },
        { 0xffffffffdddddddeULL, 0xffffffffdddddddeULL, },
        { 0x0000000066666667ULL, 0x0000000066666667ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x00000000a4fa4fa5ULL, 0x000000004fa4fa50ULL, },
        { 0xffffffffc16c16c2ULL, 0x0000000016c16c17ULL, },
        { 0xffffffffe38e38e4ULL, 0x0000000038e38e39ULL, },    /*  48  */
        { 0xffffffffe38e38e3ULL, 0x0000000038e38e38ULL, },
        { 0x0000000038e38e39ULL, 0x000000008e38e38eULL, },
        { 0xffffffff8e38e38eULL, 0xffffffffe38e38e3ULL, },
        { 0x0000000016c16c17ULL, 0x000000006c16c16cULL, },
        { 0xffffffffb05b05b0ULL, 0x0000000005b05b05ULL, },
        { 0x0000000055555555ULL, 0x0000000055555555ULL, },
        { 0xffffffff71c71c72ULL, 0x000000001c71c71cULL, },
        { 0x000000001c71c71dULL, 0xffffffffc71c71c8ULL, },    /*  56  */
        { 0x000000001c71c71cULL, 0xffffffffc71c71c7ULL, },
        { 0x0000000071c71c72ULL, 0x000000001c71c71dULL, },
        { 0xffffffffc71c71c7ULL, 0xffffffff71c71c72ULL, },
        { 0x000000004fa4fa50ULL, 0xfffffffffa4fa4fbULL, },
        { 0xffffffffe93e93e9ULL, 0xffffffff93e93e94ULL, },
        { 0x000000008e38e38eULL, 0xffffffffe38e38e4ULL, },
        { 0xffffffffaaaaaaabULL, 0xffffffffaaaaaaabULL, },
        { 0xffffffff6008918cULL, 0x000000004ceb5b52ULL, },    /*  64  */
        { 0xffffffff3ad71fc4ULL, 0x000000003627b862ULL, },
        { 0xffffffffce9b5b4cULL, 0x00000000a03be64aULL, },
        { 0xffffffff2a39047eULL, 0x00000000a22428beULL, },
        { 0xffffffffd35bab23ULL, 0x00000000147c0b0eULL, },
        { 0xffffffffae2a395bULL, 0xfffffffffdb8681eULL, },
        { 0x0000000041ee74e3ULL, 0x0000000067cc9606ULL, },
        { 0xffffffff9d8c1e15ULL, 0x0000000069b4d87aULL, },
        { 0xffffffff83f8596aULL, 0x00000000295d16f3ULL, },    /*  72  */
        { 0xffffffff5ec6e7a2ULL, 0x0000000012997403ULL, },
        { 0xfffffffff28b232aULL, 0x000000007cada1ebULL, },
        { 0xffffffff4e28cc5cULL, 0x000000007e95e45fULL, },
        { 0x0000000047ecc10dULL, 0xffffffff8f75d8ccULL, },
        { 0x0000000022bb4f45ULL, 0xffffffff78b235dcULL, },
        { 0x00000000b67f8acdULL, 0xffffffffe2c663c4ULL, },
        { 0x00000000121d33ffULL, 0xffffffffe4aea638ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_HSUB_S_D(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_HSUB_S_D(b128_random[i], b128_random[j],
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
