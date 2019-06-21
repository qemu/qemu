/*
 *  Test program for MSA instruction MULV.H
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
    char *group_name = "Int Multiply";
    char *instruction_name =  "MULV.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5556555655565556ULL, 0x5556555655565556ULL, },
        { 0xaaabaaabaaabaaabULL, 0xaaabaaabaaabaaabULL, },
        { 0x3334333433343334ULL, 0x3334333433343334ULL, },
        { 0xcccdcccdcccdcccdULL, 0xcccdcccdcccdcccdULL, },
        { 0x1c72c71d71c81c72ULL, 0xc71d71c81c72c71dULL, },
        { 0xe38f38e48e39e38fULL, 0x38e48e39e38f38e4ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5556555655565556ULL, 0x5556555655565556ULL, },    /*  16  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x38e438e438e438e4ULL, 0x38e438e438e438e4ULL, },
        { 0x1c721c721c721c72ULL, 0x1c721c721c721c72ULL, },
        { 0x7778777877787778ULL, 0x7778777877787778ULL, },
        { 0xdddedddedddedddeULL, 0xdddedddedddedddeULL, },
        { 0x684c84bea130684cULL, 0x84bea130684c84beULL, },
        { 0xed0ad098b426ed0aULL, 0xd098b426ed0ad098ULL, },
        { 0xaaabaaabaaabaaabULL, 0xaaabaaabaaabaaabULL, },    /*  24  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x1c721c721c721c72ULL, 0x1c721c721c721c72ULL, },
        { 0x8e398e398e398e39ULL, 0x8e398e398e398e39ULL, },
        { 0xbbbcbbbcbbbcbbbcULL, 0xbbbcbbbcbbbcbbbcULL, },
        { 0xeeefeeefeeefeeefULL, 0xeeefeeefeeefeeefULL, },
        { 0xb426425fd098b426ULL, 0x425fd098b426425fULL, },
        { 0xf685684cda13f685ULL, 0x684cda13f685684cULL, },
        { 0x3334333433343334ULL, 0x3334333433343334ULL, },    /*  32  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x7778777877787778ULL, 0x7778777877787778ULL, },
        { 0xbbbcbbbcbbbcbbbcULL, 0xbbbcbbbcbbbcbbbcULL, },
        { 0xc290c290c290c290ULL, 0xc290c290c290c290ULL, },
        { 0x70a470a470a470a4ULL, 0x70a470a470a470a4ULL, },
        { 0x7d2838e4f4a07d28ULL, 0x38e4f4a07d2838e4ULL, },
        { 0xb60cfa503e94b60cULL, 0xfa503e94b60cfa50ULL, },
        { 0xcccdcccdcccdcccdULL, 0xcccdcccdcccdcccdULL, },    /*  40  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xdddedddedddedddeULL, 0xdddedddedddedddeULL, },
        { 0xeeefeeefeeefeeefULL, 0xeeefeeefeeefeeefULL, },
        { 0x70a470a470a470a4ULL, 0x70a470a470a470a4ULL, },
        { 0x5c295c295c295c29ULL, 0x5c295c295c295c29ULL, },
        { 0x9f4a8e397d289f4aULL, 0x8e397d289f4a8e39ULL, },
        { 0x2d833e944fa52d83ULL, 0x3e944fa52d833e94ULL, },
        { 0x1c72c71d71c81c72ULL, 0xc71d71c81c72c71dULL, },    /*  48  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x684c84bea130684cULL, 0x84bea130684c84beULL, },
        { 0xb426425fd098b426ULL, 0x425fd098b426425fULL, },
        { 0x7d2838e4f4a07d28ULL, 0x38e4f4a07d2838e4ULL, },
        { 0x9f4a8e397d289f4aULL, 0x8e397d289f4a8e39ULL, },
        { 0x22c419492c4022c4ULL, 0x19492c4022c41949ULL, },
        { 0xf9aeadd44588f9aeULL, 0xadd44588f9aeadd4ULL, },
        { 0xe38f38e48e39e38fULL, 0x38e48e39e38f38e4ULL, },    /*  56  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xed0ad098b426ed0aULL, 0xd098b426ed0ad098ULL, },
        { 0xf685684cda13f685ULL, 0x684cda13f685684cULL, },
        { 0xb60cfa503e94b60cULL, 0xfa503e94b60cfa50ULL, },
        { 0x2d833e944fa52d83ULL, 0x3e944fa52d833e94ULL, },
        { 0xf9aeadd44588f9aeULL, 0xadd44588f9aeadd4ULL, },
        { 0xe9e18b1048b1e9e1ULL, 0x8b1048b1e9e18b10ULL, },
        { 0xcbe43290c5849000ULL, 0x837136844f198090ULL, },    /*  64  */
        { 0x2cac40e4aa466a00ULL, 0xfe61d18cb74523d0ULL, },
        { 0x2d44eb78793e6000ULL, 0x4fe806a2e7a97cf0ULL, },
        { 0x78b6f35cb6c27980ULL, 0xb6f78750ceb69f80ULL, },
        { 0x2cac40e4aa466a00ULL, 0xfe61d18cb74523d0ULL, },
        { 0x21042649c2697040ULL, 0xaa51fea465816810ULL, },
        { 0x28cc8bbef4dddc00ULL, 0xa1687ae6a695e7b0ULL, },
        { 0xcfa29fc7d323b470ULL, 0xe587adf0113e5580ULL, },
        { 0x2d44eb78793e6000ULL, 0x4fe806a2e7a97cf0ULL, },    /*  72  */
        { 0x28cc8bbef4dddc00ULL, 0xa1687ae6a695e7b0ULL, },
        { 0x0fa488e4d5614000ULL, 0x864072017939c990ULL, },
        { 0x8fc62522929f8100ULL, 0x7a585f288416d480ULL, },
        { 0x78b6f35cb6c27980ULL, 0xb6f78750ceb69f80ULL, },
        { 0xcfa29fc7d323b470ULL, 0xe587adf0113e5580ULL, },
        { 0x8fc62522929f8100ULL, 0x7a585f288416d480ULL, },
        { 0x386153290561cfc4ULL, 0x5ce136403504e400ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MULV_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MULV_H(b128_random[i], b128_random[j],
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
