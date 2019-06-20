/*
 *  Test program for MSA instruction DOTP_S.W
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
    char *group_name = "Int Dot Product";
    char *instruction_name =  "DOTP_S.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000200000002ULL, 0x0000000200000002ULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000aaac0000aaacULL, 0x0000aaac0000aaacULL, },
        { 0xffff5556ffff5556ULL, 0xffff5556ffff5556ULL, },
        { 0x0000666800006668ULL, 0x0000666800006668ULL, },
        { 0xffff999affff999aULL, 0xffff999affff999aULL, },
        { 0xffffe38f00008e3aULL, 0x000038e5ffffe38fULL, },
        { 0x00001c73ffff71c8ULL, 0xffffc71d00001c73ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000aaac0000aaacULL, 0x0000aaac0000aaacULL, },    /*  16  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x38e471c838e471c8ULL, 0x38e471c838e471c8ULL, },
        { 0xc71c38e4c71c38e4ULL, 0xc71c38e4c71c38e4ULL, },
        { 0x2222eef02222eef0ULL, 0x2222eef02222eef0ULL, },
        { 0xddddbbbcddddbbbcULL, 0xddddbbbcddddbbbcULL, },
        { 0xf684ed0a2f69097cULL, 0x12f725eef684ed0aULL, },
        { 0x097bbda2d097a130ULL, 0xed0984be097bbda2ULL, },
        { 0xffff5556ffff5556ULL, 0xffff5556ffff5556ULL, },    /*  24  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xc71c38e4c71c38e4ULL, 0xc71c38e4c71c38e4ULL, },
        { 0x38e31c7238e31c72ULL, 0x38e31c7238e31c72ULL, },
        { 0xdddd7778dddd7778ULL, 0xdddd7778dddd7778ULL, },
        { 0x2221ddde2221dddeULL, 0x2221ddde2221dddeULL, },
        { 0x097af685d09784beULL, 0xed0912f7097af685ULL, },
        { 0xf6845ed12f67d098ULL, 0x12f6425ff6845ed1ULL, },
        { 0x0000666800006668ULL, 0x0000666800006668ULL, },    /*  32  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x2222eef02222eef0ULL, 0x2222eef02222eef0ULL, },
        { 0xdddd7778dddd7778ULL, 0xdddd7778dddd7778ULL, },
        { 0x147b8520147b8520ULL, 0x147b8520147b8520ULL, },
        { 0xeb84e148eb84e148ULL, 0xeb84e148eb84e148ULL, },
        { 0xfa4fb60c1c7271c8ULL, 0x0b612d84fa4fb60cULL, },
        { 0x05b0b05ce38df4a0ULL, 0xf49f38e405b0b05cULL, },
        { 0xffff999affff999aULL, 0xffff999affff999aULL, },    /*  40  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xddddbbbcddddbbbcULL, 0xddddbbbcddddbbbcULL, },
        { 0x2221ddde2221dddeULL, 0x2221ddde2221dddeULL, },
        { 0xeb84e148eb84e148ULL, 0xeb84e148eb84e148ULL, },
        { 0x147ab852147ab852ULL, 0x147ab852147ab852ULL, },
        { 0x05b02d83e38e1c72ULL, 0xf49f0b6105b02d83ULL, },
        { 0xfa4f6c171c717d28ULL, 0x0b608e39fa4f6c17ULL, },
        { 0xffffe38f00008e3aULL, 0x000038e5ffffe38fULL, },    /*  48  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xf684ed0a2f69097cULL, 0x12f725eef684ed0aULL, },
        { 0x097af685d09784beULL, 0xed0912f7097af685ULL, },
        { 0xfa4fb60c1c7271c8ULL, 0x0b612d84fa4fb60cULL, },
        { 0x05b02d83e38e1c72ULL, 0xf49f0b6105b02d83ULL, },
        { 0x0fcd3c0d35bb4f04ULL, 0x3f3645890fcd3c0dULL, },
        { 0xf032a782ca453f36ULL, 0xc0c9f35cf032a782ULL, },
        { 0x00001c73ffff71c8ULL, 0xffffc71d00001c73ULL, },    /*  56  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x097bbda2d097a130ULL, 0xed0984be097bbda2ULL, },
        { 0xf6845ed12f67d098ULL, 0x12f6425ff6845ed1ULL, },
        { 0x05b0b05ce38df4a0ULL, 0xf49f38e405b0b05cULL, },
        { 0xfa4f6c171c717d28ULL, 0x0b608e39fa4f6c17ULL, },
        { 0xf032a782ca453f36ULL, 0xc0c9f35cf032a782ULL, },
        { 0x0fcd74f135ba3292ULL, 0x3f35d3c10fcd74f1ULL, },
        { 0x3a57fe7422c25584ULL, 0x16b6b9f518facfa9ULL, },    /*  64  */
        { 0x01f36d90f9441446ULL, 0x0286cfede5f4db15ULL, },
        { 0x2f1518bcce21d93eULL, 0x0934568af4ec6499ULL, },
        { 0xc9576c1204f83042ULL, 0xd91d3e4709b06e36ULL, },
        { 0x01f36d90f9441446ULL, 0x0286cfede5f4db15ULL, },
        { 0x0012474d242f32a9ULL, 0x13f2a8f51ca9cd91ULL, },
        { 0x0144b48a04a7d0ddULL, 0x124b1c4e04fa8e45ULL, },
        { 0xfe2a6f6923268793ULL, 0x179e9377ef4766beULL, },
        { 0x2f1518bcce21d93eULL, 0x0934568af4ec6499ULL, },    /*  72  */
        { 0x0144b48a04a7d0ddULL, 0x124b1c4e04fa8e45ULL, },
        { 0x352c988848431561ULL, 0x12e4f841217b42c9ULL, },
        { 0xd437b4e8f3b0139fULL, 0x08c7d980187d5896ULL, },
        { 0xc9576c1204f83042ULL, 0xd91d3e4709b06e36ULL, },
        { 0xfe2a6f6923268793ULL, 0x179e9377ef4766beULL, },
        { 0xd437b4e8f3b0139fULL, 0x08c7d980187d5896ULL, },
        { 0x33368b8a2619d525ULL, 0x6a47932120c31904ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_DOTP_S_W(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DOTP_S_W(b128_random[i], b128_random[j],
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
