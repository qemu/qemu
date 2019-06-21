/*
 *  Test program for MSA instruction ADD_A.H
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
    char *instruction_name =  "ADD_A.H";
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
        { 0xaaacaaacaaacaaacULL, 0xaaacaaacaaacaaacULL, },
        { 0xaaabaaabaaabaaabULL, 0xaaabaaabaaabaaabULL, },
        { 0x888a888a888a888aULL, 0x888a888a888a888aULL, },
        { 0x8889888988898889ULL, 0x8889888988898889ULL, },
        { 0x71c88e39c71e71c8ULL, 0x8e39c71e71c88e39ULL, },
        { 0x71c78e3ac71d71c7ULL, 0x8e3ac71d71c78e3aULL, },
        { 0x5556555655565556ULL, 0x5556555655565556ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xaaabaaabaaabaaabULL, 0xaaabaaabaaabaaabULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x8889888988898889ULL, 0x8889888988898889ULL, },
        { 0x8888888888888888ULL, 0x8888888888888888ULL, },
        { 0x71c78e38c71d71c7ULL, 0x8e38c71d71c78e38ULL, },
        { 0x71c68e39c71c71c6ULL, 0x8e39c71c71c68e39ULL, },
        { 0x3335333533353335ULL, 0x3335333533353335ULL, },    /*  32  */
        { 0x3334333433343334ULL, 0x3334333433343334ULL, },
        { 0x888a888a888a888aULL, 0x888a888a888a888aULL, },
        { 0x8889888988898889ULL, 0x8889888988898889ULL, },
        { 0x6668666866686668ULL, 0x6668666866686668ULL, },
        { 0x6667666766676667ULL, 0x6667666766676667ULL, },
        { 0x4fa66c17a4fc4fa6ULL, 0x6c17a4fc4fa66c17ULL, },
        { 0x4fa56c18a4fb4fa5ULL, 0x6c18a4fb4fa56c18ULL, },
        { 0x3334333433343334ULL, 0x3334333433343334ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x8889888988898889ULL, 0x8889888988898889ULL, },
        { 0x8888888888888888ULL, 0x8888888888888888ULL, },
        { 0x6667666766676667ULL, 0x6667666766676667ULL, },
        { 0x6666666666666666ULL, 0x6666666666666666ULL, },
        { 0x4fa56c16a4fb4fa5ULL, 0x6c16a4fb4fa56c16ULL, },
        { 0x4fa46c17a4fa4fa4ULL, 0x6c17a4fa4fa46c17ULL, },
        { 0x1c7338e471c91c73ULL, 0x38e471c91c7338e4ULL, },    /*  48  */
        { 0x1c7238e371c81c72ULL, 0x38e371c81c7238e3ULL, },
        { 0x71c88e39c71e71c8ULL, 0x8e39c71e71c88e39ULL, },
        { 0x71c78e38c71d71c7ULL, 0x8e38c71d71c78e38ULL, },
        { 0x4fa66c17a4fc4fa6ULL, 0x6c17a4fc4fa66c17ULL, },
        { 0x4fa56c16a4fb4fa5ULL, 0x6c16a4fb4fa56c16ULL, },
        { 0x38e471c6e39038e4ULL, 0x71c6e39038e471c6ULL, },
        { 0x38e371c7e38f38e3ULL, 0x71c7e38f38e371c7ULL, },
        { 0x1c7238e571c81c72ULL, 0x38e571c81c7238e5ULL, },    /*  56  */
        { 0x1c7138e471c71c71ULL, 0x38e471c71c7138e4ULL, },
        { 0x71c78e3ac71d71c7ULL, 0x8e3ac71d71c78e3aULL, },
        { 0x71c68e39c71c71c6ULL, 0x8e39c71c71c68e39ULL, },
        { 0x4fa56c18a4fb4fa5ULL, 0x6c18a4fb4fa56c18ULL, },
        { 0x4fa46c17a4fa4fa4ULL, 0x6c17a4fa4fa46c17ULL, },
        { 0x38e371c7e38f38e3ULL, 0x71c7e38f38e371c7ULL, },
        { 0x38e271c8e38e38e2ULL, 0x71c8e38e38e271c8ULL, },
        { 0xef2c326850c4aa80ULL, 0x96ce16bc030a9fe8ULL, },    /*  64  */
        { 0x7bd8199775f58e38ULL, 0x5e5e504416c4a2f0ULL, },
        { 0xcb3c6a8a6e93c9c0ULL, 0x733f445f565a7508ULL, },
        { 0xe7e52f81869372f2ULL, 0xbd76828658436d54ULL, },
        { 0x7bd8199775f58e38ULL, 0x5e5e504416c4a2f0ULL, },
        { 0x088400c69b2671f0ULL, 0x25ee89cc2a7ea5f8ULL, },
        { 0x57e851b993c4ad78ULL, 0x3acf7de76a147810ULL, },
        { 0x749116b0abc456aaULL, 0x8506bc0e6bfd705cULL, },
        { 0xcb3c6a8a6e93c9c0ULL, 0x733f445f565a7508ULL, },    /*  72  */
        { 0x57e851b993c4ad78ULL, 0x3acf7de76a147810ULL, },
        { 0xa74ca2ac8c62e900ULL, 0x4fb07202a9aa4a28ULL, },
        { 0xc3f567a3a4629232ULL, 0x99e7b029ab934274ULL, },
        { 0xe7e52f81869372f2ULL, 0xbd76828658436d54ULL, },
        { 0x749116b0abc456aaULL, 0x8506bc0e6bfd705cULL, },
        { 0xc3f567a3a4629232ULL, 0x99e7b029ab934274ULL, },
        { 0xe09e2c9abc623b64ULL, 0xe41eee50ad7c3ac0ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_ADD_A_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_ADD_A_H(b128_random[i], b128_random[j],
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
