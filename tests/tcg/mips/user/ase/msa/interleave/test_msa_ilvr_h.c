/*
 *  Test program for MSA instruction ILVR.H
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
    char *instruction_name =  "ILVR.H";
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
        { 0xffff8e38ffffe38eULL, 0xffffe38effff38e3ULL, },
        { 0xffff71c7ffff1c71ULL, 0xffff1c71ffffc71cULL, },
        { 0x0000ffff0000ffffULL, 0x0000ffff0000ffffULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000aaaa0000aaaaULL, 0x0000aaaa0000aaaaULL, },
        { 0x0000555500005555ULL, 0x0000555500005555ULL, },
        { 0x0000cccc0000ccccULL, 0x0000cccc0000ccccULL, },
        { 0x0000333300003333ULL, 0x0000333300003333ULL, },
        { 0x00008e380000e38eULL, 0x0000e38e000038e3ULL, },
        { 0x000071c700001c71ULL, 0x00001c710000c71cULL, },
        { 0xaaaaffffaaaaffffULL, 0xaaaaffffaaaaffffULL, },    /*  16  */
        { 0xaaaa0000aaaa0000ULL, 0xaaaa0000aaaa0000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaa5555aaaa5555ULL, 0xaaaa5555aaaa5555ULL, },
        { 0xaaaaccccaaaaccccULL, 0xaaaaccccaaaaccccULL, },
        { 0xaaaa3333aaaa3333ULL, 0xaaaa3333aaaa3333ULL, },
        { 0xaaaa8e38aaaae38eULL, 0xaaaae38eaaaa38e3ULL, },
        { 0xaaaa71c7aaaa1c71ULL, 0xaaaa1c71aaaac71cULL, },
        { 0x5555ffff5555ffffULL, 0x5555ffff5555ffffULL, },    /*  24  */
        { 0x5555000055550000ULL, 0x5555000055550000ULL, },
        { 0x5555aaaa5555aaaaULL, 0x5555aaaa5555aaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555cccc5555ccccULL, 0x5555cccc5555ccccULL, },
        { 0x5555333355553333ULL, 0x5555333355553333ULL, },
        { 0x55558e385555e38eULL, 0x5555e38e555538e3ULL, },
        { 0x555571c755551c71ULL, 0x55551c715555c71cULL, },
        { 0xccccffffccccffffULL, 0xccccffffccccffffULL, },    /*  32  */
        { 0xcccc0000cccc0000ULL, 0xcccc0000cccc0000ULL, },
        { 0xccccaaaaccccaaaaULL, 0xccccaaaaccccaaaaULL, },
        { 0xcccc5555cccc5555ULL, 0xcccc5555cccc5555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xcccc3333cccc3333ULL, 0xcccc3333cccc3333ULL, },
        { 0xcccc8e38cccce38eULL, 0xcccce38ecccc38e3ULL, },
        { 0xcccc71c7cccc1c71ULL, 0xcccc1c71ccccc71cULL, },
        { 0x3333ffff3333ffffULL, 0x3333ffff3333ffffULL, },    /*  40  */
        { 0x3333000033330000ULL, 0x3333000033330000ULL, },
        { 0x3333aaaa3333aaaaULL, 0x3333aaaa3333aaaaULL, },
        { 0x3333555533335555ULL, 0x3333555533335555ULL, },
        { 0x3333cccc3333ccccULL, 0x3333cccc3333ccccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x33338e383333e38eULL, 0x3333e38e333338e3ULL, },
        { 0x333371c733331c71ULL, 0x33331c713333c71cULL, },
        { 0x8e38ffffe38effffULL, 0xe38effff38e3ffffULL, },    /*  48  */
        { 0x8e380000e38e0000ULL, 0xe38e000038e30000ULL, },
        { 0x8e38aaaae38eaaaaULL, 0xe38eaaaa38e3aaaaULL, },
        { 0x8e385555e38e5555ULL, 0xe38e555538e35555ULL, },
        { 0x8e38cccce38eccccULL, 0xe38ecccc38e3ccccULL, },
        { 0x8e383333e38e3333ULL, 0xe38e333338e33333ULL, },
        { 0x8e388e38e38ee38eULL, 0xe38ee38e38e338e3ULL, },
        { 0x8e3871c7e38e1c71ULL, 0xe38e1c7138e3c71cULL, },
        { 0x71c7ffff1c71ffffULL, 0x1c71ffffc71cffffULL, },    /*  56  */
        { 0x71c700001c710000ULL, 0x1c710000c71c0000ULL, },
        { 0x71c7aaaa1c71aaaaULL, 0x1c71aaaac71caaaaULL, },
        { 0x71c755551c715555ULL, 0x1c715555c71c5555ULL, },
        { 0x71c7cccc1c71ccccULL, 0x1c71ccccc71cccccULL, },
        { 0x71c733331c713333ULL, 0x1c713333c71c3333ULL, },
        { 0x71c78e381c71e38eULL, 0x1c71e38ec71c38e3ULL, },
        { 0x71c771c71c711c71ULL, 0x1c711c71c71cc71cULL, },
        { 0x2862286255405540ULL, 0x886a886ae6cce6ccULL, },    /*  64  */
        { 0x28624d935540c708ULL, 0x886afbbee6cc0063ULL, },
        { 0x2862b9cf55408b80ULL, 0x886aac5ae6ccaeaaULL, },
        { 0x28625e315540e24eULL, 0x886a704fe6cc164dULL, },
        { 0x4d932862c7085540ULL, 0xfbbe886a0063e6ccULL, },
        { 0x4d934d93c708c708ULL, 0xfbbefbbe00630063ULL, },
        { 0x4d93b9cfc7088b80ULL, 0xfbbeac5a0063aeaaULL, },
        { 0x4d935e31c708e24eULL, 0xfbbe704f0063164dULL, },
        { 0xb9cf28628b805540ULL, 0xac5a886aaeaae6ccULL, },    /*  72  */
        { 0xb9cf4d938b80c708ULL, 0xac5afbbeaeaa0063ULL, },
        { 0xb9cfb9cf8b808b80ULL, 0xac5aac5aaeaaaeaaULL, },
        { 0xb9cf5e318b80e24eULL, 0xac5a704faeaa164dULL, },
        { 0x5e312862e24e5540ULL, 0x704f886a164de6ccULL, },
        { 0x5e314d93e24ec708ULL, 0x704ffbbe164d0063ULL, },
        { 0x5e31b9cfe24e8b80ULL, 0x704fac5a164daeaaULL, },
        { 0x5e315e31e24ee24eULL, 0x704f704f164d164dULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_ILVR_H(b128_pattern[i], b128_pattern[j],
                          b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_ILVR_H(b128_random[i], b128_random[j],
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
