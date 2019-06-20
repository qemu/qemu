/*
 *  Test program for MSA instruction ASUB_S.H
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
    char *group_name = "Int Subtract";
    char *instruction_name =  "ASUB_S.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5556555655565556ULL, 0x5556555655565556ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3334333433343334ULL, 0x3334333433343334ULL, },
        { 0x1c7138e471c71c71ULL, 0x38e471c71c7138e4ULL, },
        { 0x1c7238e371c81c72ULL, 0x38e371c81c7238e3ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5556555655565556ULL, 0x5556555655565556ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x3334333433343334ULL, 0x3334333433343334ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x1c7238e371c81c72ULL, 0x38e371c81c7238e3ULL, },
        { 0x1c7138e471c71c71ULL, 0x38e471c71c7138e4ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },    /*  16  */
        { 0x5556555655565556ULL, 0x5556555655565556ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xaaabaaabaaabaaabULL, 0xaaabaaabaaabaaabULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x8889888988898889ULL, 0x8889888988898889ULL, },
        { 0x38e48e391c7238e4ULL, 0x8e391c7238e48e39ULL, },
        { 0x71c71c72c71d71c7ULL, 0x1c72c71d71c71c72ULL, },
        { 0x5556555655565556ULL, 0x5556555655565556ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xaaabaaabaaabaaabULL, 0xaaabaaabaaabaaabULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x8889888988898889ULL, 0x8889888988898889ULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x71c71c72c71d71c7ULL, 0x1c72c71d71c71c72ULL, },
        { 0x38e48e391c7238e4ULL, 0x8e391c7238e48e39ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },    /*  32  */
        { 0x3334333433343334ULL, 0x3334333433343334ULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x8889888988898889ULL, 0x8889888988898889ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x6667666766676667ULL, 0x6667666766676667ULL, },
        { 0x16c26c173e9416c2ULL, 0x6c173e9416c26c17ULL, },
        { 0x4fa505b0a4fb4fa5ULL, 0x05b0a4fb4fa505b0ULL, },
        { 0x3334333433343334ULL, 0x3334333433343334ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x8889888988898889ULL, 0x8889888988898889ULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x6667666766676667ULL, 0x6667666766676667ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x4fa505b0a4fb4fa5ULL, 0x05b0a4fb4fa505b0ULL, },
        { 0x16c26c173e9416c2ULL, 0x6c173e9416c26c17ULL, },
        { 0x1c7138e471c71c71ULL, 0x38e471c71c7138e4ULL, },    /*  48  */
        { 0x1c7238e371c81c72ULL, 0x38e371c81c7238e3ULL, },
        { 0x38e48e391c7238e4ULL, 0x8e391c7238e48e39ULL, },
        { 0x71c71c72c71d71c7ULL, 0x1c72c71d71c71c72ULL, },
        { 0x16c26c173e9416c2ULL, 0x6c173e9416c26c17ULL, },
        { 0x4fa505b0a4fb4fa5ULL, 0x05b0a4fb4fa505b0ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x38e371c7e38f38e3ULL, 0x71c7e38f38e371c7ULL, },
        { 0x1c7238e371c81c72ULL, 0x38e371c81c7238e3ULL, },    /*  56  */
        { 0x1c7138e471c71c71ULL, 0x38e471c71c7138e4ULL, },
        { 0x71c71c72c71d71c7ULL, 0x1c72c71d71c71c72ULL, },
        { 0x38e48e391c7238e4ULL, 0x8e391c7238e48e39ULL, },
        { 0x4fa505b0a4fb4fa5ULL, 0x05b0a4fb4fa505b0ULL, },
        { 0x16c26c173e9416c2ULL, 0x6c173e9416c26c17ULL, },
        { 0x38e371c7e38f38e3ULL, 0x71c7e38f38e371c7ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  64  */
        { 0x7354199725318e38ULL, 0x3870504416c4a2f0ULL, },
        { 0x23f038226e93c9c0ULL, 0x238f445f53507508ULL, },
        { 0xe7e52f8135cf72f2ULL, 0xbd76828655393294ULL, },
        { 0x7354199725318e38ULL, 0x3870504416c4a2f0ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x4f6451b993c43b88ULL, 0x14e10be56a142de8ULL, },
        { 0x749115ea109e1b46ULL, 0x850632426bfd705cULL, },
        { 0x23f038226e93c9c0ULL, 0x238f445f53507508ULL, },    /*  72  */
        { 0x4f6451b993c43b88ULL, 0x14e10be56a142de8ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xc3f567a3a46256ceULL, 0x99e73e2701e94274ULL, },
        { 0xe7e52f8135cf72f2ULL, 0xbd76828655393294ULL, },
        { 0x749115ea109e1b46ULL, 0x850632426bfd705cULL, },
        { 0xc3f567a3a46256ceULL, 0x99e73e2701e94274ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_ASUB_S_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_ASUB_S_H(b128_random[i], b128_random[j],
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
