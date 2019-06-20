/*
 *  Test program for MSA instruction MULR_Q.H
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
    char *group_name = "Fixed Multiply";
    char *instruction_name =  "MULR_Q.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000010000ULL, 0x0000000100000000ULL, },
        { 0x00000000ffff0000ULL, 0x0000ffff00000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },    /*  16  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x38e438e438e438e4ULL, 0x38e438e438e438e4ULL, },
        { 0xc71cc71cc71cc71cULL, 0xc71cc71cc71cc71cULL, },
        { 0x2223222322232223ULL, 0x2223222322232223ULL, },
        { 0xdddedddedddedddeULL, 0xdddedddedddedddeULL, },
        { 0x12f7da134bdb12f7ULL, 0xda134bdb12f7da13ULL, },
        { 0xed0a25eeb425ed0aULL, 0x25eeb425ed0a25eeULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  24  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xc71cc71cc71cc71cULL, 0xc71cc71cc71cc71cULL, },
        { 0x38e338e338e338e3ULL, 0x38e338e338e338e3ULL, },
        { 0xddddddddddddddddULL, 0xddddddddddddddddULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0xed0925edb426ed09ULL, 0x25edb426ed0925edULL, },
        { 0x12f6da134bda12f6ULL, 0xda134bda12f6da13ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  32  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x2223222322232223ULL, 0x2223222322232223ULL, },
        { 0xddddddddddddddddULL, 0xddddddddddddddddULL, },
        { 0x147c147c147c147cULL, 0x147c147c147c147cULL, },
        { 0xeb85eb85eb85eb85ULL, 0xeb85eb85eb85eb85ULL, },
        { 0x0b61e93e2d840b61ULL, 0xe93e2d840b61e93eULL, },
        { 0xf49f16c2d27cf49fULL, 0x16c2d27cf49f16c2ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  40  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xdddedddedddedddeULL, 0xdddedddedddedddeULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0xeb85eb85eb85eb85ULL, 0xeb85eb85eb85eb85ULL, },
        { 0x147b147b147b147bULL, 0x147b147b147b147bULL, },
        { 0xf49f16c1d27df49fULL, 0x16c1d27df49f16c1ULL, },
        { 0x0b60e93e2d830b60ULL, 0xe93e2d830b60e93eULL, },
        { 0x0000000000010000ULL, 0x0000000100000000ULL, },    /*  48  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x12f7da134bdb12f7ULL, 0xda134bdb12f7da13ULL, },
        { 0xed0925edb426ed09ULL, 0x25edb426ed0925edULL, },
        { 0x0b61e93e2d840b61ULL, 0xe93e2d840b61e93eULL, },
        { 0xf49f16c1d27df49fULL, 0x16c1d27df49f16c1ULL, },
        { 0x0652194865240652ULL, 0x1948652406521948ULL, },
        { 0xf9aee6b79addf9aeULL, 0xe6b79addf9aee6b7ULL, },
        { 0x00000000ffff0000ULL, 0x0000ffff00000000ULL, },    /*  56  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xed0a25eeb425ed0aULL, 0x25eeb425ed0a25eeULL, },
        { 0x12f6da134bda12f6ULL, 0xda134bda12f6da13ULL, },
        { 0xf49f16c2d27cf49fULL, 0x16c2d27cf49f16c2ULL, },
        { 0x0b60e93e2d830b60ULL, 0xe93e2d830b60e93eULL, },
        { 0xf9aee6b79addf9aeULL, 0xe6b79addf9aee6b7ULL, },
        { 0x0652194965230652ULL, 0x1949652306521949ULL, },
        { 0x6fba04f60cbe38c7ULL, 0x2c6b0102000531f1ULL, },    /*  64  */
        { 0x03faffed1879da0fULL, 0x0b2cf9e2ffbfcc2aULL, },
        { 0x4e261004e9dbb269ULL, 0x1779faf00102e8d7ULL, },
        { 0x9713fb9c1db7ec39ULL, 0xbccff56b01081259ULL, },
        { 0x03faffed1879da0fULL, 0x0b2cf9e2ffbfcc2aULL, },
        { 0x002400002f04195bULL, 0x02cf2516038735cdULL, },
        { 0x02c8ffc1d57633daULL, 0x05e71eaff1eb180aULL, },
        { 0xfc44001139160d37ULL, 0xef1a4023f19aecf5ULL, },
        { 0x4e261004e9dbb269ULL, 0x1779faf00102e8d7ULL, },    /*  72  */
        { 0x02c8ffc1d57633daULL, 0x05e71eaff1eb180aULL, },
        { 0x36aa33af267e6a09ULL, 0x0c67196338390abeULL, },
        { 0xb69bf1d4cc591b07ULL, 0xdc7f3511397df77eULL, },
        { 0x9713fb9c1db7ec39ULL, 0xbccff56b01081259ULL, },
        { 0xfc44001139160d37ULL, 0xef1a4023f19aecf5ULL, },
        { 0xb69bf1d4cc591b07ULL, 0xdc7f3511397df77eULL, },
        { 0x628a03e3455006e4ULL, 0x65a36eec3ac806beULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MULR_Q_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MULR_Q_H(b128_random[i], b128_random[j],
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
