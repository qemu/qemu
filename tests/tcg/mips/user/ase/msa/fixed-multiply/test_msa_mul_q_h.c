/*
 *  Test program for MSA instruction MUL_Q.H
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
    char *instruction_name =  "MUL_Q.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000ffff00000000ULL, 0xffff00000000ffffULL, },
        { 0xffff0000ffffffffULL, 0x0000ffffffff0000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  16  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x38e438e438e438e4ULL, 0x38e438e438e438e4ULL, },
        { 0xc71cc71cc71cc71cULL, 0xc71cc71cc71cc71cULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0xddddddddddddddddULL, 0xddddddddddddddddULL, },
        { 0x12f6da134bdb12f6ULL, 0xda134bdb12f6da13ULL, },
        { 0xed0925edb425ed09ULL, 0x25edb425ed0925edULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  24  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xc71cc71cc71cc71cULL, 0xc71cc71cc71cc71cULL, },
        { 0x38e338e338e338e3ULL, 0x38e338e338e338e3ULL, },
        { 0xddddddddddddddddULL, 0xddddddddddddddddULL, },
        { 0x2221222122212221ULL, 0x2221222122212221ULL, },
        { 0xed0925ecb425ed09ULL, 0x25ecb425ed0925ecULL, },
        { 0x12f5da124bd912f5ULL, 0xda124bd912f5da12ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  32  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0xddddddddddddddddULL, 0xddddddddddddddddULL, },
        { 0x147b147b147b147bULL, 0x147b147b147b147bULL, },
        { 0xeb84eb84eb84eb84ULL, 0xeb84eb84eb84eb84ULL, },
        { 0x0b60e93e2d830b60ULL, 0xe93e2d830b60e93eULL, },
        { 0xf49f16c1d27cf49fULL, 0x16c1d27cf49f16c1ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  40  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xddddddddddddddddULL, 0xddddddddddddddddULL, },
        { 0x2221222122212221ULL, 0x2221222122212221ULL, },
        { 0xeb84eb84eb84eb84ULL, 0xeb84eb84eb84eb84ULL, },
        { 0x147a147a147a147aULL, 0x147a147a147a147aULL, },
        { 0xf49f16c1d27cf49fULL, 0x16c1d27cf49f16c1ULL, },
        { 0x0b60e93e2d820b60ULL, 0xe93e2d820b60e93eULL, },
        { 0x0000ffff00000000ULL, 0xffff00000000ffffULL, },    /*  48  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x12f6da134bdb12f6ULL, 0xda134bdb12f6da13ULL, },
        { 0xed0925ecb425ed09ULL, 0x25ecb425ed0925ecULL, },
        { 0x0b60e93e2d830b60ULL, 0xe93e2d830b60e93eULL, },
        { 0xf49f16c1d27cf49fULL, 0x16c1d27cf49f16c1ULL, },
        { 0x0652194865240652ULL, 0x1948652406521948ULL, },
        { 0xf9ade6b79adcf9adULL, 0xe6b79adcf9ade6b7ULL, },
        { 0xffff0000ffffffffULL, 0x0000ffffffff0000ULL, },    /*  56  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xed0925edb425ed09ULL, 0x25edb425ed0925edULL, },
        { 0x12f5da124bd912f5ULL, 0xda124bd912f5da12ULL, },
        { 0xf49f16c1d27cf49fULL, 0x16c1d27cf49f16c1ULL, },
        { 0x0b60e93e2d820b60ULL, 0xe93e2d820b60e93eULL, },
        { 0xf9ade6b79adcf9adULL, 0xe6b79adcf9ade6b7ULL, },
        { 0x0651194965220651ULL, 0x1949652206511949ULL, },
        { 0x6fb904f60cbd38c7ULL, 0x2c6b0102000431f1ULL, },    /*  64  */
        { 0x03faffec1879da0eULL, 0x0b2bf9e1ffbfcc2aULL, },
        { 0x4e261003e9dab268ULL, 0x1778faf00101e8d6ULL, },
        { 0x9712fb9b1db7ec38ULL, 0xbccff56b01071259ULL, },
        { 0x03faffec1879da0eULL, 0x0b2bf9e1ffbfcc2aULL, },
        { 0x002400002f03195aULL, 0x02cf2515038635ccULL, },
        { 0x02c8ffc1d57533d9ULL, 0x05e71eaef1eb1809ULL, },
        { 0xfc43001139150d37ULL, 0xef194023f19aecf4ULL, },
        { 0x4e261003e9dab268ULL, 0x1778faf00101e8d6ULL, },    /*  72  */
        { 0x02c8ffc1d57533d9ULL, 0x05e71eaef1eb1809ULL, },
        { 0x36aa33af267d6a08ULL, 0x0c67196238380abdULL, },
        { 0xb69bf1d4cc591b07ULL, 0xdc7e3510397df77dULL, },
        { 0x9712fb9b1db7ec38ULL, 0xbccff56b01071259ULL, },
        { 0xfc43001139150d37ULL, 0xef194023f19aecf4ULL, },
        { 0xb69bf1d4cc591b07ULL, 0xdc7e3510397df77dULL, },
        { 0x628a03e2455006e3ULL, 0x65a26eec3ac806bdULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MUL_Q_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MUL_Q_H(b128_random[i], b128_random[j],
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
