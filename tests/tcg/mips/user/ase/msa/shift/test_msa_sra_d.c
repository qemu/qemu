/*
 *  Test program for MSA instruction SRA.D
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
    char *group_name = "Shift";
    char *instruction_name =  "SRA.D";
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
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xffffffffffeaaaaaULL, 0xffffffffffeaaaaaULL, },
        { 0xfffffd5555555555ULL, 0xfffffd5555555555ULL, },
        { 0xfffaaaaaaaaaaaaaULL, 0xfffaaaaaaaaaaaaaULL, },
        { 0xfffffffffffff555ULL, 0xfffffffffffff555ULL, },
        { 0xfffeaaaaaaaaaaaaULL, 0xfffffffff5555555ULL, },
        { 0xffffffffffffd555ULL, 0xfffffffaaaaaaaaaULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x0000000000155555ULL, 0x0000000000155555ULL, },
        { 0x000002aaaaaaaaaaULL, 0x000002aaaaaaaaaaULL, },
        { 0x0005555555555555ULL, 0x0005555555555555ULL, },
        { 0x0000000000000aaaULL, 0x0000000000000aaaULL, },
        { 0x0001555555555555ULL, 0x000000000aaaaaaaULL, },
        { 0x0000000000002aaaULL, 0x0000000555555555ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xfffffffffff33333ULL, 0xfffffffffff33333ULL, },
        { 0xfffffe6666666666ULL, 0xfffffe6666666666ULL, },
        { 0xfffcccccccccccccULL, 0xfffcccccccccccccULL, },
        { 0xfffffffffffff999ULL, 0xfffffffffffff999ULL, },
        { 0xffff333333333333ULL, 0xfffffffff9999999ULL, },
        { 0xffffffffffffe666ULL, 0xfffffffcccccccccULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x00000000000cccccULL, 0x00000000000cccccULL, },
        { 0x0000019999999999ULL, 0x0000019999999999ULL, },
        { 0x0003333333333333ULL, 0x0003333333333333ULL, },
        { 0x0000000000000666ULL, 0x0000000000000666ULL, },
        { 0x0000ccccccccccccULL, 0x0000000006666666ULL, },
        { 0x0000000000001999ULL, 0x0000000333333333ULL, },
        { 0xffffffffffffffffULL, 0x0000000000000000ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xfffffffffff8e38eULL, 0x00000000000e38e3ULL, },
        { 0xffffff1c71c71c71ULL, 0x000001c71c71c71cULL, },
        { 0xfffe38e38e38e38eULL, 0x00038e38e38e38e3ULL, },
        { 0xfffffffffffffc71ULL, 0x000000000000071cULL, },
        { 0xffff8e38e38e38e3ULL, 0x00000000071c71c7ULL, },
        { 0xfffffffffffff1c7ULL, 0x000000038e38e38eULL, },
        { 0x0000000000000000ULL, 0xffffffffffffffffULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x0000000000071c71ULL, 0xfffffffffff1c71cULL, },
        { 0x000000e38e38e38eULL, 0xfffffe38e38e38e3ULL, },
        { 0x0001c71c71c71c71ULL, 0xfffc71c71c71c71cULL, },
        { 0x000000000000038eULL, 0xfffffffffffff8e3ULL, },
        { 0x000071c71c71c71cULL, 0xfffffffff8e38e38ULL, },
        { 0x0000000000000e38ULL, 0xfffffffc71c71c71ULL, },
        { 0x886ae6cc28625540ULL, 0x0004b670b5efe7bbULL, },    /*  64  */
        { 0xff886ae6cc286255ULL, 0x0000000000000004ULL, },
        { 0x886ae6cc28625540ULL, 0x000004b670b5efe7ULL, },
        { 0xfffe21ab9b30a189ULL, 0x000000004b670b5eULL, },
        { 0xfbbe00634d93c708ULL, 0x00012f7bb1a153f5ULL, },
        { 0xfffbbe00634d93c7ULL, 0x0000000000000001ULL, },
        { 0xfbbe00634d93c708ULL, 0x0000012f7bb1a153ULL, },
        { 0xffffeef8018d364fULL, 0x0000000012f7bb1aULL, },
        { 0xac5aaeaab9cf8b80ULL, 0x00027d8c6ffab2b2ULL, },    /*  72  */
        { 0xffac5aaeaab9cf8bULL, 0x0000000000000002ULL, },
        { 0xac5aaeaab9cf8b80ULL, 0x0000027d8c6ffab2ULL, },
        { 0xfffeb16abaaae73eULL, 0x0000000027d8c6ffULL, },
        { 0x704f164d5e31e24eULL, 0xfff8df188d8a942eULL, },
        { 0x00704f164d5e31e2ULL, 0xfffffffffffffff8ULL, },
        { 0x704f164d5e31e24eULL, 0xfffff8df188d8a94ULL, },
        { 0x0001c13c593578c7ULL, 0xffffffff8df188d8ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRA_D(b128_pattern[i], b128_pattern[j],
                         b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRA_D(b128_random[i], b128_random[j],
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
