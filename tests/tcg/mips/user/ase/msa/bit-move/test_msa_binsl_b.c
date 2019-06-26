/*
 *  Test program for MSA instruction BINSL.B
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
    char *instruction_name =  "BINSL.B";
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
        { 0x9c71e7cc71675471ULL, 0x4767015ffe71c70cULL, },    /*  64  */
        { 0x8c6be7cc29675571ULL, 0x4767015ffe7ba70cULL, },
        { 0x8c6be7cc29625571ULL, 0x4b670b5efe7bb30cULL, },
        { 0x8c6ae7cc29625541ULL, 0x4b670b5efe7bb30cULL, },
        { 0x8caa01642982c541ULL, 0x1bf7bb1a143b33fcULL, },
        { 0xfcbe01644d92c741ULL, 0x1bf7bb1a143f53fcULL, },
        { 0xfcbe01644d93c741ULL, 0x12f7bb1a143f53fcULL, },
        { 0xfcbe01604d93c709ULL, 0x12f7bb1a143f53fcULL, },
        { 0xfc5eafa8cdd38b89ULL, 0x22d8cbfeaa2f5314ULL, },    /*  72  */
        { 0xac5aafa8b9c38b89ULL, 0x22d8cbfeaa2b3314ULL, },
        { 0xac5aafa8b9cf8b89ULL, 0x27d8c7ffaa2b2714ULL, },
        { 0xac5aafa8b9cf8b81ULL, 0x27d8c7ffaa2b2714ULL, },
        { 0x2c5a1748392fe301ULL, 0x87f187d9a84ba7a4ULL, },
        { 0x7c4e17485d3fe201ULL, 0x87f187d9a842e7a4ULL, },
        { 0x744e17485d31e201ULL, 0x8df189d8a842e3a4ULL, },
        { 0x744f174c5f31e24fULL, 0x8df189d8a842e3a4ULL, },
        { 0x744f174c5f31e24fULL, 0x8df189d8a842e3a4ULL, },    /*  80  */
        { 0x744f174c5f31e24fULL, 0x8df189d8a842e3a4ULL, },
        { 0x744f174c5f31e24fULL, 0x8df189d8a842e3a4ULL, },
        { 0x744f174c5f31e24fULL, 0x8df189d8a842e3a4ULL, },
        { 0x744f174c5f31e24fULL, 0x8df189d8a842e3a4ULL, },
        { 0x744f174c5f31e24fULL, 0x8df189d8a842e3a4ULL, },
        { 0x744f174c5f31e24fULL, 0x8df189d8a842e3a4ULL, },
        { 0x744f174c5f31e24fULL, 0x8df189d8a842e3a4ULL, },
        { 0x744f174c5f31e24fULL, 0x8df189d8a842e3a4ULL, },    /*  88  */
        { 0x744f174c5f31e24fULL, 0x8df189d8a842e3a4ULL, },
        { 0x744f174c5f31e24fULL, 0x8df189d8a842e3a4ULL, },
        { 0x744f174c5f31e24fULL, 0x8df189d8a842e3a4ULL, },
        { 0x744f174c5f31e24fULL, 0x8df189d8a842e3a4ULL, },
        { 0x744f174c5f31e24fULL, 0x8df189d8a842e3a4ULL, },
        { 0x744f174c5f31e24fULL, 0x8df189d8a842e3a4ULL, },
        { 0x744f174c5f31e24fULL, 0x8df189d8a842e3a4ULL, },
        { 0x8c6ae6cc28714240ULL, 0x49710958a862b30cULL, },    /*  96  */
        { 0x8c6ae6cc28714240ULL, 0x49710958a862b30cULL, },
        { 0x8c6ae6cc28714240ULL, 0x49710958a862b30cULL, },
        { 0x8c6ae6cc28714240ULL, 0x49710958a862b30cULL, },
        { 0xfcaa006428b1c240ULL, 0x09f18958282253fcULL, },
        { 0xfcaa006428b1c240ULL, 0x09f18958282253fcULL, },
        { 0xfcaa006428b1c240ULL, 0x09f18958282253fcULL, },
        { 0xfcaa006428b1c240ULL, 0x09f18958282253fcULL, },
        { 0xac4a80aca8f182c0ULL, 0x09f1c9d8a8222314ULL, },    /* 104  */
        { 0xac4a80aca8f182c0ULL, 0x09f1c9d8a8222314ULL, },
        { 0xac4a80aca8f182c0ULL, 0x09f1c9d8a8222314ULL, },
        { 0xac4a80aca8f182c0ULL, 0x09f1c9d8a8222314ULL, },
        { 0x744a004c2831e240ULL, 0x89f189d8a842e3a4ULL, },
        { 0x744a004c2831e240ULL, 0x89f189d8a842e3a4ULL, },
        { 0x744a004c2831e240ULL, 0x89f189d8a842e3a4ULL, },
        { 0x744a004c2831e240ULL, 0x89f189d8a842e3a4ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_BINSL_B(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_BINSL_B(b128_random[i], b128_random[j],
                           b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                        (PATTERN_INPUTS_SHORT_COUNT)) +
                                       RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_BINSL_B__DDT(b128_random[i], b128_random[j],
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
            do_msa_BINSL_B__DSD(b128_random[i], b128_random[j],
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
