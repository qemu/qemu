/*
 *  Test program for MSA instruction NOR.V
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
    char *group_name = "Logic";
    char *instruction_name =  "NOR.V";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  16  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x1111111111111111ULL, 0x1111111111111111ULL, },
        { 0x4444444444444444ULL, 0x4444444444444444ULL, },
        { 0x1451451451451451ULL, 0x4514514514514514ULL, },
        { 0x4104104104104104ULL, 0x1041041041041041ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  24  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x8888888888888888ULL, 0x8888888888888888ULL, },
        { 0x0820820820820820ULL, 0x8208208208208208ULL, },
        { 0xa28a28a28a28a28aULL, 0x28a28a28a28a28a2ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  32  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x1111111111111111ULL, 0x1111111111111111ULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x1031031031031031ULL, 0x0310310310310310ULL, },
        { 0x2302302302302302ULL, 0x3023023023023023ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  40  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x4444444444444444ULL, 0x4444444444444444ULL, },
        { 0x8888888888888888ULL, 0x8888888888888888ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x0c40c40c40c40c40ULL, 0xc40c40c40c40c40cULL, },
        { 0xc08c08c08c08c08cULL, 0x08c08c08c08c08c0ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  48  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1451451451451451ULL, 0x4514514514514514ULL, },
        { 0x0820820820820820ULL, 0x8208208208208208ULL, },
        { 0x1031031031031031ULL, 0x0310310310310310ULL, },
        { 0x0c40c40c40c40c40ULL, 0xc40c40c40c40c40cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  56  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x4104104104104104ULL, 0x1041041041041041ULL, },
        { 0xa28a28a28a28a28aULL, 0x28a28a28a28a28a2ULL, },
        { 0x2302302302302302ULL, 0x3023023023023023ULL, },
        { 0xc08c08c08c08c08cULL, 0x08c08c08c08c08c0ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x77951933d79daabfULL, 0xb498f4a101844ff3ULL, },    /*  64  */
        { 0x04011910920c28b7ULL, 0xa40844a100800d03ULL, },
        { 0x538511114610203fULL, 0x9000300000844ae3ULL, },
        { 0x07900932818c08b1ULL, 0x3008742100840d53ULL, },
        { 0x04011910920c28b7ULL, 0xa40844a100800d03ULL, },
        { 0x0441ff9cb26c38f7ULL, 0xed0844e5eac0ad03ULL, },
        { 0x0001511402203077ULL, 0xc800000040c08803ULL, },
        { 0x0400e990a04c18b1ULL, 0x6008442542800d03ULL, },
        { 0x538511114610203fULL, 0x9000300000844ae3ULL, },    /*  72  */
        { 0x0001511402203077ULL, 0xc800000040c08803ULL, },
        { 0x53a551554630747fULL, 0xd827390054d4daebULL, },
        { 0x03a0411000001431ULL, 0x500631005494184bULL, },
        { 0x07900932818c08b1ULL, 0x3008742100840d53ULL, },
        { 0x0400e990a04c18b1ULL, 0x6008442542800d03ULL, },
        { 0x03a0411000001431ULL, 0x500631005494184bULL, },
        { 0x8fb0e9b2a1ce1db1ULL, 0x720e772756bd1d5fULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_NOR_V(b128_pattern[i], b128_pattern[j],
                         b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_NOR_V(b128_random[i], b128_random[j],
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
