/*
 *  Test program for MSA instruction AND.V
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
    char *instruction_name =  "AND.V";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },    /*  16  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x8888888888888888ULL, 0x8888888888888888ULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0xa28a28a28a28a28aULL, 0x28a28a28a28a28a2ULL, },
        { 0x0820820820820820ULL, 0x8208208208208208ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },    /*  24  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x4444444444444444ULL, 0x4444444444444444ULL, },
        { 0x1111111111111111ULL, 0x1111111111111111ULL, },
        { 0x4104104104104104ULL, 0x1041041041041041ULL, },
        { 0x1451451451451451ULL, 0x4514514514514514ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },    /*  32  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x8888888888888888ULL, 0x8888888888888888ULL, },
        { 0x4444444444444444ULL, 0x4444444444444444ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xc08c08c08c08c08cULL, 0x08c08c08c08c08c0ULL, },
        { 0x0c40c40c40c40c40ULL, 0xc40c40c40c40c40cULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },    /*  40  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x1111111111111111ULL, 0x1111111111111111ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x2302302302302302ULL, 0x3023023023023023ULL, },
        { 0x1031031031031031ULL, 0x0310310310310310ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },    /*  48  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xa28a28a28a28a28aULL, 0x28a28a28a28a28a2ULL, },
        { 0x4104104104104104ULL, 0x1041041041041041ULL, },
        { 0xc08c08c08c08c08cULL, 0x08c08c08c08c08c0ULL, },
        { 0x2302302302302302ULL, 0x3023023023023023ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },    /*  56  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0820820820820820ULL, 0x8208208208208208ULL, },
        { 0x1451451451451451ULL, 0x4514514514514514ULL, },
        { 0x0c40c40c40c40c40ULL, 0xc40c40c40c40c40cULL, },
        { 0x1031031031031031ULL, 0x0310310310310310ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x886ae6cc28625540ULL, 0x4b670b5efe7bb00cULL, },    /*  64  */
        { 0x882a004008024500ULL, 0x02670b1a143b100cULL, },
        { 0x884aa68828420100ULL, 0x0340025eaa2b2004ULL, },
        { 0x004a064c08204040ULL, 0x09610858a842a000ULL, },
        { 0x882a004008024500ULL, 0x02670b1a143b100cULL, },
        { 0xfbbe00634d93c708ULL, 0x12f7bb1a153f52fcULL, },
        { 0xa81a002209838300ULL, 0x02d0821a012b0014ULL, },
        { 0x700e00414c11c208ULL, 0x00f18818010242a0ULL, },
        { 0x884aa68828420100ULL, 0x0340025eaa2b2004ULL, },    /*  72  */
        { 0xa81a002209838300ULL, 0x02d0821a012b0014ULL, },
        { 0xac5aaeaab9cf8b80ULL, 0x27d8c6ffab2b2514ULL, },
        { 0x204a060818018200ULL, 0x05d080d8a9022000ULL, },
        { 0x004a064c08204040ULL, 0x09610858a842a000ULL, },
        { 0x700e00414c11c208ULL, 0x00f18818010242a0ULL, },
        { 0x204a060818018200ULL, 0x05d080d8a9022000ULL, },
        { 0x704f164d5e31e24eULL, 0x8df188d8a942e2a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_AND_V(b128_pattern[i], b128_pattern[j],
                         b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_AND_V(b128_random[i], b128_random[j],
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
