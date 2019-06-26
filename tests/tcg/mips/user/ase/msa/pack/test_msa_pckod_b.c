/*
 *  Test program for MSA instruction PCKOD.B
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
    char *group_name = "Pack";
    char *instruction_name =  "PCKOD.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0x0000000000000000ULL, 0xffffffffffffffffULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xffffffffffffffffULL, },
        { 0x5555555555555555ULL, 0xffffffffffffffffULL, },
        { 0xccccccccccccccccULL, 0xffffffffffffffffULL, },
        { 0x3333333333333333ULL, 0xffffffffffffffffULL, },
        { 0x388ee338e3388ee3ULL, 0xffffffffffffffffULL, },
        { 0xc7711cc71cc7711cULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0x0000000000000000ULL, },
        { 0x5555555555555555ULL, 0x0000000000000000ULL, },
        { 0xccccccccccccccccULL, 0x0000000000000000ULL, },
        { 0x3333333333333333ULL, 0x0000000000000000ULL, },
        { 0x388ee338e3388ee3ULL, 0x0000000000000000ULL, },
        { 0xc7711cc71cc7711cULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xaaaaaaaaaaaaaaaaULL, },    /*  16  */
        { 0x0000000000000000ULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x5555555555555555ULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xccccccccccccccccULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x3333333333333333ULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x388ee338e3388ee3ULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xc7711cc71cc7711cULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xffffffffffffffffULL, 0x5555555555555555ULL, },    /*  24  */
        { 0x0000000000000000ULL, 0x5555555555555555ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xccccccccccccccccULL, 0x5555555555555555ULL, },
        { 0x3333333333333333ULL, 0x5555555555555555ULL, },
        { 0x388ee338e3388ee3ULL, 0x5555555555555555ULL, },
        { 0xc7711cc71cc7711cULL, 0x5555555555555555ULL, },
        { 0xffffffffffffffffULL, 0xccccccccccccccccULL, },    /*  32  */
        { 0x0000000000000000ULL, 0xccccccccccccccccULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xccccccccccccccccULL, },
        { 0x5555555555555555ULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x3333333333333333ULL, 0xccccccccccccccccULL, },
        { 0x388ee338e3388ee3ULL, 0xccccccccccccccccULL, },
        { 0xc7711cc71cc7711cULL, 0xccccccccccccccccULL, },
        { 0xffffffffffffffffULL, 0x3333333333333333ULL, },    /*  40  */
        { 0x0000000000000000ULL, 0x3333333333333333ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0x3333333333333333ULL, },
        { 0x5555555555555555ULL, 0x3333333333333333ULL, },
        { 0xccccccccccccccccULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x388ee338e3388ee3ULL, 0x3333333333333333ULL, },
        { 0xc7711cc71cc7711cULL, 0x3333333333333333ULL, },
        { 0xffffffffffffffffULL, 0x388ee338e3388ee3ULL, },    /*  48  */
        { 0x0000000000000000ULL, 0x388ee338e3388ee3ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0x388ee338e3388ee3ULL, },
        { 0x5555555555555555ULL, 0x388ee338e3388ee3ULL, },
        { 0xccccccccccccccccULL, 0x388ee338e3388ee3ULL, },
        { 0x3333333333333333ULL, 0x388ee338e3388ee3ULL, },
        { 0x388ee338e3388ee3ULL, 0x388ee338e3388ee3ULL, },
        { 0xc7711cc71cc7711cULL, 0x388ee338e3388ee3ULL, },
        { 0xffffffffffffffffULL, 0xc7711cc71cc7711cULL, },    /*  56  */
        { 0x0000000000000000ULL, 0xc7711cc71cc7711cULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xc7711cc71cc7711cULL, },
        { 0x5555555555555555ULL, 0xc7711cc71cc7711cULL, },
        { 0xccccccccccccccccULL, 0xc7711cc71cc7711cULL, },
        { 0x3333333333333333ULL, 0xc7711cc71cc7711cULL, },
        { 0x388ee338e3388ee3ULL, 0xc7711cc71cc7711cULL, },
        { 0xc7711cc71cc7711cULL, 0xc7711cc71cc7711cULL, },
        { 0x4b0bfeb088e62855ULL, 0x4b0bfeb088e62855ULL, },    /*  64  */
        { 0x12bb1552fb004dc7ULL, 0x4b0bfeb088e62855ULL, },
        { 0x27c6ab25acaeb98bULL, 0x4b0bfeb088e62855ULL, },
        { 0x8d88a9e270165ee2ULL, 0x4b0bfeb088e62855ULL, },
        { 0x4b0bfeb088e62855ULL, 0x12bb1552fb004dc7ULL, },
        { 0x12bb1552fb004dc7ULL, 0x12bb1552fb004dc7ULL, },
        { 0x27c6ab25acaeb98bULL, 0x12bb1552fb004dc7ULL, },
        { 0x8d88a9e270165ee2ULL, 0x12bb1552fb004dc7ULL, },
        { 0x4b0bfeb088e62855ULL, 0x27c6ab25acaeb98bULL, },    /*  72  */
        { 0x12bb1552fb004dc7ULL, 0x27c6ab25acaeb98bULL, },
        { 0x27c6ab25acaeb98bULL, 0x27c6ab25acaeb98bULL, },
        { 0x8d88a9e270165ee2ULL, 0x27c6ab25acaeb98bULL, },
        { 0x4b0bfeb088e62855ULL, 0x8d88a9e270165ee2ULL, },
        { 0x12bb1552fb004dc7ULL, 0x8d88a9e270165ee2ULL, },
        { 0x27c6ab25acaeb98bULL, 0x8d88a9e270165ee2ULL, },
        { 0x8d88a9e270165ee2ULL, 0x8d88a9e270165ee2ULL, },
        { 0x4b0bfeb088e62855ULL, 0x8da9705e8da9705eULL, },    /*  80  */
        { 0x12bb1552fb004dc7ULL, 0x8d708d704bfe8828ULL, },
        { 0x27c6ab25acaeb98bULL, 0x8d8d4b881215fb4dULL, },
        { 0x8d88a9e270165ee2ULL, 0x8d4b12fb27abacb9ULL, },
        { 0x4b0bfeb088e62855ULL, 0x8d1227ac8da9705eULL, },
        { 0x12bb1552fb004dc7ULL, 0x8d278d704bfe8828ULL, },
        { 0x27c6ab25acaeb98bULL, 0x8d8d4b881215fb4dULL, },
        { 0x8d88a9e270165ee2ULL, 0x8d4b12fb27abacb9ULL, },
        { 0x4b0bfeb088e62855ULL, 0x8d1227ac8da9705eULL, },    /*  88  */
        { 0x12bb1552fb004dc7ULL, 0x8d278d704bfe8828ULL, },
        { 0x27c6ab25acaeb98bULL, 0x8d8d4b881215fb4dULL, },
        { 0x8d88a9e270165ee2ULL, 0x8d4b12fb27abacb9ULL, },
        { 0x4b0bfeb088e62855ULL, 0x8d1227ac8da9705eULL, },
        { 0x12bb1552fb004dc7ULL, 0x8d278d704bfe8828ULL, },
        { 0x27c6ab25acaeb98bULL, 0x8d8d4b881215fb4dULL, },
        { 0x8d88a9e270165ee2ULL, 0x8d4b12fb27abacb9ULL, },
        { 0x8d1227ac8da9705eULL, 0x4b0bfeb088e62855ULL, },    /*  96  */
        { 0x4bfe88288d278d70ULL, 0x4b0bfeb088e62855ULL, },
        { 0x4bfe88284b888d8dULL, 0x4b0bfeb088e62855ULL, },
        { 0x4bfe88284b884b8dULL, 0x4b0bfeb088e62855ULL, },
        { 0x4bfe88284b884b4bULL, 0x12bb1552fb004dc7ULL, },
        { 0x1215fb4d4b884b4bULL, 0x12bb1552fb004dc7ULL, },
        { 0x1215fb4d12fb4b4bULL, 0x12bb1552fb004dc7ULL, },
        { 0x1215fb4d12fb124bULL, 0x12bb1552fb004dc7ULL, },
        { 0x1215fb4d12fb1212ULL, 0x27c6ab25acaeb98bULL, },    /* 104  */
        { 0x27abacb912fb1212ULL, 0x27c6ab25acaeb98bULL, },
        { 0x27abacb927ac1212ULL, 0x27c6ab25acaeb98bULL, },
        { 0x27abacb927ac2712ULL, 0x27c6ab25acaeb98bULL, },
        { 0x27abacb927ac2727ULL, 0x8d88a9e270165ee2ULL, },
        { 0x8da9705e27ac2727ULL, 0x8d88a9e270165ee2ULL, },
        { 0x8da9705e8d702727ULL, 0x8d88a9e270165ee2ULL, },
        { 0x8da9705e8d708d27ULL, 0x8d88a9e270165ee2ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_PCKOD_B(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_PCKOD_B(b128_random[i], b128_random[j],
                           b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                        (PATTERN_INPUTS_SHORT_COUNT)) +
                                       RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_PCKOD_B__DDT(b128_random[i], b128_random[j],
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
            do_msa_PCKOD_B__DSD(b128_random[i], b128_random[j],
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
