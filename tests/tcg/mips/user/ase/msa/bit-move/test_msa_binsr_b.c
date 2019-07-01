/*
 *  Test program for MSA instruction BINSR.B
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
    char *instruction_name =  "BINSR.B";
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
        { 0x1c72e60c70c21570ULL, 0xcb677bde7e7bc60cULL, },    /*  64  */
        { 0x186ae60c68c25570ULL, 0xcb677bde7e7bc00cULL, },
        { 0x086ae60c68625570ULL, 0x4b670b5e7e7bf00cULL, },
        { 0x086ae60c28625540ULL, 0x4b670b5e7e7bf00cULL, },
        { 0x096e800329634740ULL, 0x42f70b1a157ff01cULL, },
        { 0x0b3e80030d63c740ULL, 0x42f70b1a153ff21cULL, },
        { 0x1b3e80030d93c740ULL, 0x12f73b1a153fd21cULL, },
        { 0x1bbe80234d93c708ULL, 0x12f73b1a153fd21cULL, },
        { 0x1abaae2a4d97cb08ULL, 0x17d8367f2b3bd314ULL, },    /*  72  */
        { 0x1cdaae2a799f8b08ULL, 0x17d8367f2b2bd514ULL, },
        { 0x0cdaae2a79cf8b08ULL, 0x27d846ff2b2be514ULL, },
        { 0x0c5aae2a39cf8b00ULL, 0x27d846ff2b2be514ULL, },
        { 0x0c5f962d38c9a200ULL, 0x2df148d82922e400ULL, },
        { 0x004f962d1ec1e200ULL, 0x2df148d82942e200ULL, },
        { 0x104f962d1e31e200ULL, 0x8df108d82942e200ULL, },
        { 0x104f960d5e31e24eULL, 0x8df108d82942e200ULL, },
        { 0x104f960d5e31e24eULL, 0x8df108d82942e200ULL, },    /*  80  */
        { 0x104f960d5e31e24eULL, 0x8df108d82942e200ULL, },
        { 0x104f960d5e31e24eULL, 0x8df108d82942e200ULL, },
        { 0x104f960d5e31e24eULL, 0x8df108d82942e200ULL, },
        { 0x104f960d5e31e24eULL, 0x8df108d82942e200ULL, },
        { 0x104f960d5e31e24eULL, 0x8df108d82942e200ULL, },
        { 0x104f960d5e31e24eULL, 0x8df108d82942e200ULL, },
        { 0x104f960d5e31e24eULL, 0x8df108d82942e200ULL, },
        { 0x104f960d5e31e24eULL, 0x8df108d82942e200ULL, },    /*  88  */
        { 0x104f960d5e31e24eULL, 0x8df108d82942e200ULL, },
        { 0x104f960d5e31e24eULL, 0x8df108d82942e200ULL, },
        { 0x104f960d5e31e24eULL, 0x8df108d82942e200ULL, },
        { 0x104f960d5e31e24eULL, 0x8df108d82942e200ULL, },
        { 0x104f960d5e31e24eULL, 0x8df108d82942e200ULL, },
        { 0x104f960d5e31e24eULL, 0x8df108d82942e200ULL, },
        { 0x104f960d5e31e24eULL, 0x8df108d82942e200ULL, },
        { 0x106ae60c2832e540ULL, 0x8bf309d82a43e000ULL, },    /*  96  */
        { 0x106ae60c2832d540ULL, 0x8bf70bd82e4be000ULL, },
        { 0x106ae60c2832d540ULL, 0x8b670bd87e4be000ULL, },
        { 0x106ae60c2832d540ULL, 0x8b670bd87e4be000ULL, },
        { 0x116e80032933c740ULL, 0x82f70bd8154fe000ULL, },
        { 0x133e80032933c740ULL, 0x82f70bd8153fe000ULL, },
        { 0x1b3e80032933c740ULL, 0x82f70bd8153fe000ULL, },
        { 0x1b3e80032933c740ULL, 0x82f70bd8153fe000ULL, },
        { 0x1c5a800a293f8b40ULL, 0x87d806d92b2be100ULL, },    /* 104  */
        { 0x0c5a800a29cf8b40ULL, 0x27d846db2b2be100ULL, },
        { 0x0c5a800a29cf8b40ULL, 0x27d846df2b2be100ULL, },
        { 0x0c5a800a29cf8b40ULL, 0x27d846ff2b2be100ULL, },
        { 0x105f800d2a318240ULL, 0x8dd908d82922e200ULL, },
        { 0x104f800d2e318240ULL, 0x8dd908d82922e200ULL, },
        { 0x104f800d5e318240ULL, 0x8dd908d82922e200ULL, },
        { 0x104f800d5e318240ULL, 0x8dd908d82922e200ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_BINSR_B(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_BINSR_B(b128_random[i], b128_random[j],
                           b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                        (PATTERN_INPUTS_SHORT_COUNT)) +
                                       RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_BINSR_B__DDT(b128_random[i], b128_random[j],
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
            do_msa_BINSR_B__DSD(b128_random[i], b128_random[j],
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
