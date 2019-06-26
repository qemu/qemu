/*
 *  Test program for MSA instruction BINSL.W
 *
 *  Copyright (C) 2019  Wave Computing, Inc.
 *  Copyright (C) 2019  Aleksandar Markovic <amarkovic@wavecomp.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *`
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
            3 * (RANDOM_INPUTS_SHORT_COUNT) * (RANDOM_INPUTS_SHORT_COUNT))


int32_t main(void)
{
    char *isa_ase_name = "MSA";
    char *group_name = "Bit Move";
    char *instruction_name =  "BINSL.W";
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
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x8869c71c71c71c71ULL, 0x4b670b5ffe79c71cULL, },    /*  64  */
        { 0x8869c71c28471c71ULL, 0x4b670b5ffe7bb00cULL, },
        { 0x8869c71c28471c71ULL, 0x4b670b5efe7bb00cULL, },
        { 0x8869c71c28631c71ULL, 0x4b670b5efe7bb00cULL, },
        { 0xfbb9c71c28631c71ULL, 0x12f7bb1a153bb00cULL, },
        { 0xfbb9c71c4de31c71ULL, 0x12f7bb1a153f52fcULL, },
        { 0xfbb9c71c4de31c71ULL, 0x12f7bb1a153f52fcULL, },
        { 0xfbbdc71c4d931c71ULL, 0x12f7bb1a153f52fcULL, },
        { 0xac5dc71ccd931c71ULL, 0x27d8c6feab2f52fcULL, },    /*  72  */
        { 0xac5dc71cb9931c71ULL, 0x27d8c6feab2b2514ULL, },
        { 0xac5dc71cb9931c71ULL, 0x27d8c6ffab2b2514ULL, },
        { 0xac59c71cb9cf1c71ULL, 0x27d8c6ffab2b2514ULL, },
        { 0x7049c71c39cf1c71ULL, 0x8df188d9a9432514ULL, },
        { 0x7049c71c5e4f1c71ULL, 0x8df188d9a942e2a4ULL, },
        { 0x7049c71c5e4f1c71ULL, 0x8df188d8a942e2a4ULL, },
        { 0x704dc71c5e311c71ULL, 0x8df188d8a942e2a4ULL, },
        { 0x704dc71c5e311c71ULL, 0x8df188d8a942e2a4ULL, },    /*  80  */
        { 0x704dc71c5e311c71ULL, 0x8df188d8a942e2a4ULL, },
        { 0x704dc71c5e311c71ULL, 0x8df188d8a942e2a4ULL, },
        { 0x704dc71c5e311c71ULL, 0x8df188d8a942e2a4ULL, },
        { 0x704dc71c5e311c71ULL, 0x8df188d8a942e2a4ULL, },
        { 0x704dc71c5e311c71ULL, 0x8df188d8a942e2a4ULL, },
        { 0x704dc71c5e311c71ULL, 0x8df188d8a942e2a4ULL, },
        { 0x704dc71c5e311c71ULL, 0x8df188d8a942e2a4ULL, },
        { 0x704dc71c5e311c71ULL, 0x8df188d8a942e2a4ULL, },    /*  88  */
        { 0x704dc71c5e311c71ULL, 0x8df188d8a942e2a4ULL, },
        { 0x704dc71c5e311c71ULL, 0x8df188d8a942e2a4ULL, },
        { 0x704dc71c5e311c71ULL, 0x8df188d8a942e2a4ULL, },
        { 0x704dc71c5e311c71ULL, 0x8df188d8a942e2a4ULL, },
        { 0x704dc71c5e311c71ULL, 0x8df188d8a942e2a4ULL, },
        { 0x704dc71c5e311c71ULL, 0x8df188d8a942e2a4ULL, },
        { 0x704dc71c5e311c71ULL, 0x8df188d8a942e2a4ULL, },
        { 0x886ae6cc28625c71ULL, 0x4b670b58f942e2a4ULL, },    /*  96  */
        { 0x886ae6cc28625c71ULL, 0x4b670b58f942e2a4ULL, },
        { 0x886ae6cc28625c71ULL, 0x4b670b58f942e2a4ULL, },
        { 0x886ae6cc28625c71ULL, 0x4b670b58f942e2a4ULL, },
        { 0xfbbae6cc4d93dc71ULL, 0x12f7bb581142e2a4ULL, },
        { 0xfbbae6cc4d93dc71ULL, 0x12f7bb581142e2a4ULL, },
        { 0xfbbae6cc4d93dc71ULL, 0x12f7bb581142e2a4ULL, },
        { 0xfbbae6cc4d93dc71ULL, 0x12f7bb581142e2a4ULL, },
        { 0xac5ae6ccb9cf9c71ULL, 0x27d8c6d8a942e2a4ULL, },    /* 104  */
        { 0xac5ae6ccb9cf9c71ULL, 0x27d8c6d8a942e2a4ULL, },
        { 0xac5ae6ccb9cf9c71ULL, 0x27d8c6d8a942e2a4ULL, },
        { 0xac5ae6ccb9cf9c71ULL, 0x27d8c6d8a942e2a4ULL, },
        { 0x704ae6cc5e31dc71ULL, 0x8df188d8a942e2a4ULL, },
        { 0x704ae6cc5e31dc71ULL, 0x8df188d8a942e2a4ULL, },
        { 0x704ae6cc5e31dc71ULL, 0x8df188d8a942e2a4ULL, },
        { 0x704ae6cc5e31dc71ULL, 0x8df188d8a942e2a4ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_BINSL_W(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_BINSL_W(b128_random[i], b128_random[j],
                           b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                        (PATTERN_INPUTS_SHORT_COUNT)) +
                                       RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_BINSL_W__DDT(b128_random[i], b128_random[j],
                                b128_result[
                                    ((PATTERN_INPUTS_SHORT_COUNT) *
                                     (PATTERN_INPUTS_SHORT_COUNT)) +
                                    ((RANDOM_INPUTS_SHORT_COUNT) *
                                     (RANDOM_INPUTS_SHORT_COUNT)) +
                                    RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_BINSL_W__DSD(b128_random[i], b128_random[j],
                                b128_result[
                                    ((PATTERN_INPUTS_SHORT_COUNT) *
                                     (PATTERN_INPUTS_SHORT_COUNT)) +
                                    (2 * (RANDOM_INPUTS_SHORT_COUNT) *
                                     (RANDOM_INPUTS_SHORT_COUNT)) +
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
