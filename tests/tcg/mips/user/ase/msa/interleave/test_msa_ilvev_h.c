/*
 *  Test program for MSA instruction ILVEV.H
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
    char *group_name = "Interleave";
    char *instruction_name =  "ILVEV.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0xffff0000ffff0000ULL, 0xffff0000ffff0000ULL, },
        { 0xffffaaaaffffaaaaULL, 0xffffaaaaffffaaaaULL, },
        { 0xffff5555ffff5555ULL, 0xffff5555ffff5555ULL, },
        { 0xffffccccffffccccULL, 0xffffccccffffccccULL, },
        { 0xffff3333ffff3333ULL, 0xffff3333ffff3333ULL, },
        { 0xffff38e3ffffe38eULL, 0xffff8e38ffff38e3ULL, },
        { 0xffffc71cffff1c71ULL, 0xffff71c7ffffc71cULL, },
        { 0x0000ffff0000ffffULL, 0x0000ffff0000ffffULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000aaaa0000aaaaULL, 0x0000aaaa0000aaaaULL, },
        { 0x0000555500005555ULL, 0x0000555500005555ULL, },
        { 0x0000cccc0000ccccULL, 0x0000cccc0000ccccULL, },
        { 0x0000333300003333ULL, 0x0000333300003333ULL, },
        { 0x000038e30000e38eULL, 0x00008e38000038e3ULL, },
        { 0x0000c71c00001c71ULL, 0x000071c70000c71cULL, },
        { 0xaaaaffffaaaaffffULL, 0xaaaaffffaaaaffffULL, },    /*  16  */
        { 0xaaaa0000aaaa0000ULL, 0xaaaa0000aaaa0000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaa5555aaaa5555ULL, 0xaaaa5555aaaa5555ULL, },
        { 0xaaaaccccaaaaccccULL, 0xaaaaccccaaaaccccULL, },
        { 0xaaaa3333aaaa3333ULL, 0xaaaa3333aaaa3333ULL, },
        { 0xaaaa38e3aaaae38eULL, 0xaaaa8e38aaaa38e3ULL, },
        { 0xaaaac71caaaa1c71ULL, 0xaaaa71c7aaaac71cULL, },
        { 0x5555ffff5555ffffULL, 0x5555ffff5555ffffULL, },    /*  24  */
        { 0x5555000055550000ULL, 0x5555000055550000ULL, },
        { 0x5555aaaa5555aaaaULL, 0x5555aaaa5555aaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555cccc5555ccccULL, 0x5555cccc5555ccccULL, },
        { 0x5555333355553333ULL, 0x5555333355553333ULL, },
        { 0x555538e35555e38eULL, 0x55558e38555538e3ULL, },
        { 0x5555c71c55551c71ULL, 0x555571c75555c71cULL, },
        { 0xccccffffccccffffULL, 0xccccffffccccffffULL, },    /*  32  */
        { 0xcccc0000cccc0000ULL, 0xcccc0000cccc0000ULL, },
        { 0xccccaaaaccccaaaaULL, 0xccccaaaaccccaaaaULL, },
        { 0xcccc5555cccc5555ULL, 0xcccc5555cccc5555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xcccc3333cccc3333ULL, 0xcccc3333cccc3333ULL, },
        { 0xcccc38e3cccce38eULL, 0xcccc8e38cccc38e3ULL, },
        { 0xccccc71ccccc1c71ULL, 0xcccc71c7ccccc71cULL, },
        { 0x3333ffff3333ffffULL, 0x3333ffff3333ffffULL, },    /*  40  */
        { 0x3333000033330000ULL, 0x3333000033330000ULL, },
        { 0x3333aaaa3333aaaaULL, 0x3333aaaa3333aaaaULL, },
        { 0x3333555533335555ULL, 0x3333555533335555ULL, },
        { 0x3333cccc3333ccccULL, 0x3333cccc3333ccccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x333338e33333e38eULL, 0x33338e38333338e3ULL, },
        { 0x3333c71c33331c71ULL, 0x333371c73333c71cULL, },
        { 0x38e3ffffe38effffULL, 0x8e38ffff38e3ffffULL, },    /*  48  */
        { 0x38e30000e38e0000ULL, 0x8e38000038e30000ULL, },
        { 0x38e3aaaae38eaaaaULL, 0x8e38aaaa38e3aaaaULL, },
        { 0x38e35555e38e5555ULL, 0x8e38555538e35555ULL, },
        { 0x38e3cccce38eccccULL, 0x8e38cccc38e3ccccULL, },
        { 0x38e33333e38e3333ULL, 0x8e38333338e33333ULL, },
        { 0x38e338e3e38ee38eULL, 0x8e388e3838e338e3ULL, },
        { 0x38e3c71ce38e1c71ULL, 0x8e3871c738e3c71cULL, },
        { 0xc71cffff1c71ffffULL, 0x71c7ffffc71cffffULL, },    /*  56  */
        { 0xc71c00001c710000ULL, 0x71c70000c71c0000ULL, },
        { 0xc71caaaa1c71aaaaULL, 0x71c7aaaac71caaaaULL, },
        { 0xc71c55551c715555ULL, 0x71c75555c71c5555ULL, },
        { 0xc71ccccc1c71ccccULL, 0x71c7ccccc71cccccULL, },
        { 0xc71c33331c713333ULL, 0x71c73333c71c3333ULL, },
        { 0xc71c38e31c71e38eULL, 0x71c78e38c71c38e3ULL, },
        { 0xc71cc71c1c711c71ULL, 0x71c771c7c71cc71cULL, },
        { 0xe6cce6cc55405540ULL, 0x0b5e0b5eb00cb00cULL, },    /*  64  */
        { 0xe6cc00635540c708ULL, 0x0b5ebb1ab00c52fcULL, },
        { 0xe6ccaeaa55408b80ULL, 0x0b5ec6ffb00c2514ULL, },
        { 0xe6cc164d5540e24eULL, 0x0b5e88d8b00ce2a0ULL, },
        { 0x0063e6ccc7085540ULL, 0xbb1a0b5e52fcb00cULL, },
        { 0x00630063c708c708ULL, 0xbb1abb1a52fc52fcULL, },
        { 0x0063aeaac7088b80ULL, 0xbb1ac6ff52fc2514ULL, },
        { 0x0063164dc708e24eULL, 0xbb1a88d852fce2a0ULL, },
        { 0xaeaae6cc8b805540ULL, 0xc6ff0b5e2514b00cULL, },    /*  72  */
        { 0xaeaa00638b80c708ULL, 0xc6ffbb1a251452fcULL, },
        { 0xaeaaaeaa8b808b80ULL, 0xc6ffc6ff25142514ULL, },
        { 0xaeaa164d8b80e24eULL, 0xc6ff88d82514e2a0ULL, },
        { 0x164de6cce24e5540ULL, 0x88d80b5ee2a0b00cULL, },
        { 0x164d0063e24ec708ULL, 0x88d8bb1ae2a052fcULL, },
        { 0x164daeaae24e8b80ULL, 0x88d8c6ffe2a02514ULL, },
        { 0x164d164de24ee24eULL, 0x88d888d8e2a0e2a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_ILVEV_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_ILVEV_H(b128_random[i], b128_random[j],
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
