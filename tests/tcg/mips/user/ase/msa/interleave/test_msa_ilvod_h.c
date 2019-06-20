/*
 *  Test program for MSA instruction ILVOD.H
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
    char *instruction_name =  "ILVOD.H";
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
        { 0xffffe38effff8e38ULL, 0xffff38e3ffffe38eULL, },
        { 0xffff1c71ffff71c7ULL, 0xffffc71cffff1c71ULL, },
        { 0x0000ffff0000ffffULL, 0x0000ffff0000ffffULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000aaaa0000aaaaULL, 0x0000aaaa0000aaaaULL, },
        { 0x0000555500005555ULL, 0x0000555500005555ULL, },
        { 0x0000cccc0000ccccULL, 0x0000cccc0000ccccULL, },
        { 0x0000333300003333ULL, 0x0000333300003333ULL, },
        { 0x0000e38e00008e38ULL, 0x000038e30000e38eULL, },
        { 0x00001c71000071c7ULL, 0x0000c71c00001c71ULL, },
        { 0xaaaaffffaaaaffffULL, 0xaaaaffffaaaaffffULL, },    /*  16  */
        { 0xaaaa0000aaaa0000ULL, 0xaaaa0000aaaa0000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaa5555aaaa5555ULL, 0xaaaa5555aaaa5555ULL, },
        { 0xaaaaccccaaaaccccULL, 0xaaaaccccaaaaccccULL, },
        { 0xaaaa3333aaaa3333ULL, 0xaaaa3333aaaa3333ULL, },
        { 0xaaaae38eaaaa8e38ULL, 0xaaaa38e3aaaae38eULL, },
        { 0xaaaa1c71aaaa71c7ULL, 0xaaaac71caaaa1c71ULL, },
        { 0x5555ffff5555ffffULL, 0x5555ffff5555ffffULL, },    /*  24  */
        { 0x5555000055550000ULL, 0x5555000055550000ULL, },
        { 0x5555aaaa5555aaaaULL, 0x5555aaaa5555aaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555cccc5555ccccULL, 0x5555cccc5555ccccULL, },
        { 0x5555333355553333ULL, 0x5555333355553333ULL, },
        { 0x5555e38e55558e38ULL, 0x555538e35555e38eULL, },
        { 0x55551c71555571c7ULL, 0x5555c71c55551c71ULL, },
        { 0xccccffffccccffffULL, 0xccccffffccccffffULL, },    /*  32  */
        { 0xcccc0000cccc0000ULL, 0xcccc0000cccc0000ULL, },
        { 0xccccaaaaccccaaaaULL, 0xccccaaaaccccaaaaULL, },
        { 0xcccc5555cccc5555ULL, 0xcccc5555cccc5555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xcccc3333cccc3333ULL, 0xcccc3333cccc3333ULL, },
        { 0xcccce38ecccc8e38ULL, 0xcccc38e3cccce38eULL, },
        { 0xcccc1c71cccc71c7ULL, 0xccccc71ccccc1c71ULL, },
        { 0x3333ffff3333ffffULL, 0x3333ffff3333ffffULL, },    /*  40  */
        { 0x3333000033330000ULL, 0x3333000033330000ULL, },
        { 0x3333aaaa3333aaaaULL, 0x3333aaaa3333aaaaULL, },
        { 0x3333555533335555ULL, 0x3333555533335555ULL, },
        { 0x3333cccc3333ccccULL, 0x3333cccc3333ccccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333e38e33338e38ULL, 0x333338e33333e38eULL, },
        { 0x33331c71333371c7ULL, 0x3333c71c33331c71ULL, },
        { 0xe38effff8e38ffffULL, 0x38e3ffffe38effffULL, },    /*  48  */
        { 0xe38e00008e380000ULL, 0x38e30000e38e0000ULL, },
        { 0xe38eaaaa8e38aaaaULL, 0x38e3aaaae38eaaaaULL, },
        { 0xe38e55558e385555ULL, 0x38e35555e38e5555ULL, },
        { 0xe38ecccc8e38ccccULL, 0x38e3cccce38eccccULL, },
        { 0xe38e33338e383333ULL, 0x38e33333e38e3333ULL, },
        { 0xe38ee38e8e388e38ULL, 0x38e338e3e38ee38eULL, },
        { 0xe38e1c718e3871c7ULL, 0x38e3c71ce38e1c71ULL, },
        { 0x1c71ffff71c7ffffULL, 0xc71cffff1c71ffffULL, },    /*  56  */
        { 0x1c71000071c70000ULL, 0xc71c00001c710000ULL, },
        { 0x1c71aaaa71c7aaaaULL, 0xc71caaaa1c71aaaaULL, },
        { 0x1c71555571c75555ULL, 0xc71c55551c715555ULL, },
        { 0x1c71cccc71c7ccccULL, 0xc71ccccc1c71ccccULL, },
        { 0x1c71333371c73333ULL, 0xc71c33331c713333ULL, },
        { 0x1c71e38e71c78e38ULL, 0xc71c38e31c71e38eULL, },
        { 0x1c711c7171c771c7ULL, 0xc71cc71c1c711c71ULL, },
        { 0x886a886a28622862ULL, 0x4b674b67fe7bfe7bULL, },    /*  64  */
        { 0x886afbbe28624d93ULL, 0x4b6712f7fe7b153fULL, },
        { 0x886aac5a2862b9cfULL, 0x4b6727d8fe7bab2bULL, },
        { 0x886a704f28625e31ULL, 0x4b678df1fe7ba942ULL, },
        { 0xfbbe886a4d932862ULL, 0x12f74b67153ffe7bULL, },
        { 0xfbbefbbe4d934d93ULL, 0x12f712f7153f153fULL, },
        { 0xfbbeac5a4d93b9cfULL, 0x12f727d8153fab2bULL, },
        { 0xfbbe704f4d935e31ULL, 0x12f78df1153fa942ULL, },
        { 0xac5a886ab9cf2862ULL, 0x27d84b67ab2bfe7bULL, },    /*  72  */
        { 0xac5afbbeb9cf4d93ULL, 0x27d812f7ab2b153fULL, },
        { 0xac5aac5ab9cfb9cfULL, 0x27d827d8ab2bab2bULL, },
        { 0xac5a704fb9cf5e31ULL, 0x27d88df1ab2ba942ULL, },
        { 0x704f886a5e312862ULL, 0x8df14b67a942fe7bULL, },
        { 0x704ffbbe5e314d93ULL, 0x8df112f7a942153fULL, },
        { 0x704fac5a5e31b9cfULL, 0x8df127d8a942ab2bULL, },
        { 0x704f704f5e315e31ULL, 0x8df18df1a942a942ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_ILVOD_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_ILVOD_H(b128_random[i], b128_random[j],
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
