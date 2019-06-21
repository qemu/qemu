/*
 *  Test program for MSA instruction ILVL.H
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
    char *instruction_name =  "ILVL.H";
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
        { 0xffffe38effff38e3ULL, 0xffff38e3ffff8e38ULL, },
        { 0xffff1c71ffffc71cULL, 0xffffc71cffff71c7ULL, },
        { 0x0000ffff0000ffffULL, 0x0000ffff0000ffffULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000aaaa0000aaaaULL, 0x0000aaaa0000aaaaULL, },
        { 0x0000555500005555ULL, 0x0000555500005555ULL, },
        { 0x0000cccc0000ccccULL, 0x0000cccc0000ccccULL, },
        { 0x0000333300003333ULL, 0x0000333300003333ULL, },
        { 0x0000e38e000038e3ULL, 0x000038e300008e38ULL, },
        { 0x00001c710000c71cULL, 0x0000c71c000071c7ULL, },
        { 0xaaaaffffaaaaffffULL, 0xaaaaffffaaaaffffULL, },    /*  16  */
        { 0xaaaa0000aaaa0000ULL, 0xaaaa0000aaaa0000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaa5555aaaa5555ULL, 0xaaaa5555aaaa5555ULL, },
        { 0xaaaaccccaaaaccccULL, 0xaaaaccccaaaaccccULL, },
        { 0xaaaa3333aaaa3333ULL, 0xaaaa3333aaaa3333ULL, },
        { 0xaaaae38eaaaa38e3ULL, 0xaaaa38e3aaaa8e38ULL, },
        { 0xaaaa1c71aaaac71cULL, 0xaaaac71caaaa71c7ULL, },
        { 0x5555ffff5555ffffULL, 0x5555ffff5555ffffULL, },    /*  24  */
        { 0x5555000055550000ULL, 0x5555000055550000ULL, },
        { 0x5555aaaa5555aaaaULL, 0x5555aaaa5555aaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555cccc5555ccccULL, 0x5555cccc5555ccccULL, },
        { 0x5555333355553333ULL, 0x5555333355553333ULL, },
        { 0x5555e38e555538e3ULL, 0x555538e355558e38ULL, },
        { 0x55551c715555c71cULL, 0x5555c71c555571c7ULL, },
        { 0xccccffffccccffffULL, 0xccccffffccccffffULL, },    /*  32  */
        { 0xcccc0000cccc0000ULL, 0xcccc0000cccc0000ULL, },
        { 0xccccaaaaccccaaaaULL, 0xccccaaaaccccaaaaULL, },
        { 0xcccc5555cccc5555ULL, 0xcccc5555cccc5555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xcccc3333cccc3333ULL, 0xcccc3333cccc3333ULL, },
        { 0xcccce38ecccc38e3ULL, 0xcccc38e3cccc8e38ULL, },
        { 0xcccc1c71ccccc71cULL, 0xccccc71ccccc71c7ULL, },
        { 0x3333ffff3333ffffULL, 0x3333ffff3333ffffULL, },    /*  40  */
        { 0x3333000033330000ULL, 0x3333000033330000ULL, },
        { 0x3333aaaa3333aaaaULL, 0x3333aaaa3333aaaaULL, },
        { 0x3333555533335555ULL, 0x3333555533335555ULL, },
        { 0x3333cccc3333ccccULL, 0x3333cccc3333ccccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333e38e333338e3ULL, 0x333338e333338e38ULL, },
        { 0x33331c713333c71cULL, 0x3333c71c333371c7ULL, },
        { 0xe38effff38e3ffffULL, 0x38e3ffff8e38ffffULL, },    /*  48  */
        { 0xe38e000038e30000ULL, 0x38e300008e380000ULL, },
        { 0xe38eaaaa38e3aaaaULL, 0x38e3aaaa8e38aaaaULL, },
        { 0xe38e555538e35555ULL, 0x38e355558e385555ULL, },
        { 0xe38ecccc38e3ccccULL, 0x38e3cccc8e38ccccULL, },
        { 0xe38e333338e33333ULL, 0x38e333338e383333ULL, },
        { 0xe38ee38e38e338e3ULL, 0x38e338e38e388e38ULL, },
        { 0xe38e1c7138e3c71cULL, 0x38e3c71c8e3871c7ULL, },
        { 0x1c71ffffc71cffffULL, 0xc71cffff71c7ffffULL, },    /*  56  */
        { 0x1c710000c71c0000ULL, 0xc71c000071c70000ULL, },
        { 0x1c71aaaac71caaaaULL, 0xc71caaaa71c7aaaaULL, },
        { 0x1c715555c71c5555ULL, 0xc71c555571c75555ULL, },
        { 0x1c71ccccc71cccccULL, 0xc71ccccc71c7ccccULL, },
        { 0x1c713333c71c3333ULL, 0xc71c333371c73333ULL, },
        { 0x1c71e38ec71c38e3ULL, 0xc71c38e371c78e38ULL, },
        { 0x1c711c71c71cc71cULL, 0xc71cc71c71c771c7ULL, },
        { 0xfe7bfe7bb00cb00cULL, 0x4b674b670b5e0b5eULL, },    /*  64  */
        { 0xfe7b153fb00c52fcULL, 0x4b6712f70b5ebb1aULL, },
        { 0xfe7bab2bb00c2514ULL, 0x4b6727d80b5ec6ffULL, },
        { 0xfe7ba942b00ce2a0ULL, 0x4b678df10b5e88d8ULL, },
        { 0x153ffe7b52fcb00cULL, 0x12f74b67bb1a0b5eULL, },
        { 0x153f153f52fc52fcULL, 0x12f712f7bb1abb1aULL, },
        { 0x153fab2b52fc2514ULL, 0x12f727d8bb1ac6ffULL, },
        { 0x153fa94252fce2a0ULL, 0x12f78df1bb1a88d8ULL, },
        { 0xab2bfe7b2514b00cULL, 0x27d84b67c6ff0b5eULL, },    /*  72  */
        { 0xab2b153f251452fcULL, 0x27d812f7c6ffbb1aULL, },
        { 0xab2bab2b25142514ULL, 0x27d827d8c6ffc6ffULL, },
        { 0xab2ba9422514e2a0ULL, 0x27d88df1c6ff88d8ULL, },
        { 0xa942fe7be2a0b00cULL, 0x8df14b6788d80b5eULL, },
        { 0xa942153fe2a052fcULL, 0x8df112f788d8bb1aULL, },
        { 0xa942ab2be2a02514ULL, 0x8df127d888d8c6ffULL, },
        { 0xa942a942e2a0e2a0ULL, 0x8df18df188d888d8ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_ILVL_H(b128_pattern[i], b128_pattern[j],
                          b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_ILVL_H(b128_random[i], b128_random[j],
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
