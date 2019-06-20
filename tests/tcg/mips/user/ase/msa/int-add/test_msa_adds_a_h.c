/*
 *  Test program for MSA instruction ADDS_A.H
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
    char *group_name = "Int Add";
    char *instruction_name =  "ADDS_A.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0002000200020002ULL, 0x0002000200020002ULL, },    /*   0  */
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x5557555755575557ULL, 0x5557555755575557ULL, },
        { 0x5556555655565556ULL, 0x5556555655565556ULL, },
        { 0x3335333533353335ULL, 0x3335333533353335ULL, },
        { 0x3334333433343334ULL, 0x3334333433343334ULL, },
        { 0x1c7338e471c91c73ULL, 0x38e471c91c7338e4ULL, },
        { 0x1c7238e571c81c72ULL, 0x38e571c81c7238e5ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5556555655565556ULL, 0x5556555655565556ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x3334333433343334ULL, 0x3334333433343334ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x1c7238e371c81c72ULL, 0x38e371c81c7238e3ULL, },
        { 0x1c7138e471c71c71ULL, 0x38e471c71c7138e4ULL, },
        { 0x5557555755575557ULL, 0x5557555755575557ULL, },    /*  16  */
        { 0x5556555655565556ULL, 0x5556555655565556ULL, },
        { 0x7fff7fff7fff7fffULL, 0x7fff7fff7fff7fffULL, },
        { 0x7fff7fff7fff7fffULL, 0x7fff7fff7fff7fffULL, },
        { 0x7fff7fff7fff7fffULL, 0x7fff7fff7fff7fffULL, },
        { 0x7fff7fff7fff7fffULL, 0x7fff7fff7fff7fffULL, },
        { 0x71c87fff7fff71c8ULL, 0x7fff7fff71c87fffULL, },
        { 0x71c77fff7fff71c7ULL, 0x7fff7fff71c77fffULL, },
        { 0x5556555655565556ULL, 0x5556555655565556ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x7fff7fff7fff7fffULL, 0x7fff7fff7fff7fffULL, },
        { 0x7fff7fff7fff7fffULL, 0x7fff7fff7fff7fffULL, },
        { 0x7fff7fff7fff7fffULL, 0x7fff7fff7fff7fffULL, },
        { 0x7fff7fff7fff7fffULL, 0x7fff7fff7fff7fffULL, },
        { 0x71c77fff7fff71c7ULL, 0x7fff7fff71c77fffULL, },
        { 0x71c67fff7fff71c6ULL, 0x7fff7fff71c67fffULL, },
        { 0x3335333533353335ULL, 0x3335333533353335ULL, },    /*  32  */
        { 0x3334333433343334ULL, 0x3334333433343334ULL, },
        { 0x7fff7fff7fff7fffULL, 0x7fff7fff7fff7fffULL, },
        { 0x7fff7fff7fff7fffULL, 0x7fff7fff7fff7fffULL, },
        { 0x6668666866686668ULL, 0x6668666866686668ULL, },
        { 0x6667666766676667ULL, 0x6667666766676667ULL, },
        { 0x4fa66c177fff4fa6ULL, 0x6c177fff4fa66c17ULL, },
        { 0x4fa56c187fff4fa5ULL, 0x6c187fff4fa56c18ULL, },
        { 0x3334333433343334ULL, 0x3334333433343334ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x7fff7fff7fff7fffULL, 0x7fff7fff7fff7fffULL, },
        { 0x7fff7fff7fff7fffULL, 0x7fff7fff7fff7fffULL, },
        { 0x6667666766676667ULL, 0x6667666766676667ULL, },
        { 0x6666666666666666ULL, 0x6666666666666666ULL, },
        { 0x4fa56c167fff4fa5ULL, 0x6c167fff4fa56c16ULL, },
        { 0x4fa46c177fff4fa4ULL, 0x6c177fff4fa46c17ULL, },
        { 0x1c7338e471c91c73ULL, 0x38e471c91c7338e4ULL, },    /*  48  */
        { 0x1c7238e371c81c72ULL, 0x38e371c81c7238e3ULL, },
        { 0x71c87fff7fff71c8ULL, 0x7fff7fff71c87fffULL, },
        { 0x71c77fff7fff71c7ULL, 0x7fff7fff71c77fffULL, },
        { 0x4fa66c177fff4fa6ULL, 0x6c177fff4fa66c17ULL, },
        { 0x4fa56c167fff4fa5ULL, 0x6c167fff4fa56c16ULL, },
        { 0x38e471c67fff38e4ULL, 0x71c67fff38e471c6ULL, },
        { 0x38e371c77fff38e3ULL, 0x71c77fff38e371c7ULL, },
        { 0x1c7238e571c81c72ULL, 0x38e571c81c7238e5ULL, },    /*  56  */
        { 0x1c7138e471c71c71ULL, 0x38e471c71c7138e4ULL, },
        { 0x71c77fff7fff71c7ULL, 0x7fff7fff71c77fffULL, },
        { 0x71c67fff7fff71c6ULL, 0x7fff7fff71c67fffULL, },
        { 0x4fa56c187fff4fa5ULL, 0x6c187fff4fa56c18ULL, },
        { 0x4fa46c177fff4fa4ULL, 0x6c177fff4fa46c17ULL, },
        { 0x38e371c77fff38e3ULL, 0x71c77fff38e371c7ULL, },
        { 0x38e271c87fff38e2ULL, 0x71c87fff38e271c8ULL, },
        { 0x7fff326850c47fffULL, 0x7fff16bc030a7fffULL, },    /*  64  */
        { 0x7bd8199775f57fffULL, 0x5e5e504416c47fffULL, },
        { 0x7fff6a8a6e937fffULL, 0x733f445f565a7508ULL, },
        { 0x7fff2f817fff72f2ULL, 0x7fff7fff58436d54ULL, },
        { 0x7bd8199775f57fffULL, 0x5e5e504416c47fffULL, },
        { 0x088400c67fff71f0ULL, 0x25ee7fff2a7e7fffULL, },
        { 0x57e851b97fff7fffULL, 0x3acf7de76a147810ULL, },
        { 0x749116b07fff56aaULL, 0x7fff7fff6bfd705cULL, },
        { 0x7fff6a8a6e937fffULL, 0x733f445f565a7508ULL, },    /*  72  */
        { 0x57e851b97fff7fffULL, 0x3acf7de76a147810ULL, },
        { 0x7fff7fff7fff7fffULL, 0x4fb072027fff4a28ULL, },
        { 0x7fff67a37fff7fffULL, 0x7fff7fff7fff4274ULL, },
        { 0x7fff2f817fff72f2ULL, 0x7fff7fff58436d54ULL, },
        { 0x749116b07fff56aaULL, 0x7fff7fff6bfd705cULL, },
        { 0x7fff67a37fff7fffULL, 0x7fff7fff7fff4274ULL, },
        { 0x7fff2c9a7fff3b64ULL, 0x7fff7fff7fff3ac0ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_ADDS_A_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_ADDS_A_H(b128_random[i], b128_random[j],
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
