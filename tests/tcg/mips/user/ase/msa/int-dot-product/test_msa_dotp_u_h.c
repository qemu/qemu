/*
 *  Test program for MSA instruction DOTP_U.H
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
    char *group_name = "Int Dot Product";
    char *instruction_name =  "DOTP_U.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xfc02fc02fc02fc02ULL, 0xfc02fc02fc02fc02ULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x52ac52ac52ac52acULL, 0x52ac52ac52ac52acULL, },
        { 0xa956a956a956a956ULL, 0xa956a956a956a956ULL, },
        { 0x9668966896689668ULL, 0x9668966896689668ULL, },
        { 0x659a659a659a659aULL, 0x659a659a659a659aULL, },
        { 0x6f8f19e5c53a6f8fULL, 0x19e5c53a6f8f19e5ULL, },
        { 0x8c73e21d36c88c73ULL, 0xe21d36c88c73e21dULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x52ac52ac52ac52acULL, 0x52ac52ac52ac52acULL, },    /*  16  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xe1c8e1c8e1c8e1c8ULL, 0xe1c8e1c8e1c8e1c8ULL, },
        { 0x70e470e470e470e4ULL, 0x70e470e470e470e4ULL, },
        { 0x0ef00ef00ef00ef0ULL, 0x0ef00ef00ef00ef0ULL, },
        { 0x43bc43bc43bc43bcULL, 0x43bc43bc43bc43bcULL, },
        { 0xf50abbee837cf50aULL, 0xbbee837cf50abbeeULL, },
        { 0x5da296becf305da2ULL, 0x96becf305da296beULL, },
        { 0xa956a956a956a956ULL, 0xa956a956a956a956ULL, },    /*  24  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x70e470e470e470e4ULL, 0x70e470e470e470e4ULL, },
        { 0x3872387238723872ULL, 0x3872387238723872ULL, },
        { 0x8778877887788778ULL, 0x8778877887788778ULL, },
        { 0x21de21de21de21deULL, 0x21de21de21de21deULL, },
        { 0x7a855df741be7a85ULL, 0x5df741be7a855df7ULL, },
        { 0x2ed14b5f67982ed1ULL, 0x4b5f67982ed14b5fULL, },
        { 0x9668966896689668ULL, 0x9668966896689668ULL, },    /*  32  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0ef00ef00ef00ef0ULL, 0x0ef00ef00ef00ef0ULL, },
        { 0x8778877887788778ULL, 0x8778877887788778ULL, },
        { 0x4520452045204520ULL, 0x4520452045204520ULL, },
        { 0x5148514851485148ULL, 0x5148514851485148ULL, },
        { 0x260ce1849dc8260cULL, 0xe1849dc8260ce184ULL, },
        { 0x705cb4e4f8a0705cULL, 0xb4e4f8a0705cb4e4ULL, },
        { 0x659a659a659a659aULL, 0x659a659a659a659aULL, },    /*  40  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x43bc43bc43bc43bcULL, 0x43bc43bc43bc43bcULL, },
        { 0x21de21de21de21deULL, 0x21de21de21de21deULL, },
        { 0x5148514851485148ULL, 0x5148514851485148ULL, },
        { 0x1452145214521452ULL, 0x1452145214521452ULL, },
        { 0x4983386127724983ULL, 0x3861277249833861ULL, },
        { 0x1c172d393e281c17ULL, 0x2d393e281c172d39ULL, },
        { 0x6f8f19e5c53a6f8fULL, 0x19e5c53a6f8f19e5ULL, },    /*  48  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xf50abbee837cf50aULL, 0xbbee837cf50abbeeULL, },
        { 0x7a855df741be7a85ULL, 0x5df741be7a855df7ULL, },
        { 0x260ce1849dc8260cULL, 0xe1849dc8260ce184ULL, },
        { 0x4983386127724983ULL, 0x3861277249833861ULL, },
        { 0x180dd5895b04180dULL, 0xd5895b04180dd589ULL, },
        { 0x5782445c6a365782ULL, 0x445c6a365782445cULL, },
        { 0x8c73e21d36c88c73ULL, 0xe21d36c88c73e21dULL, },    /*  56  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5da296becf305da2ULL, 0x96becf305da296beULL, },
        { 0x2ed14b5f67982ed1ULL, 0x4b5f67982ed14b5fULL, },
        { 0x705cb4e4f8a0705cULL, 0xb4e4f8a0705cb4e4ULL, },
        { 0x1c172d393e281c17ULL, 0x2d393e281c172d39ULL, },
        { 0x5782445c6a365782ULL, 0x445c6a365782445cULL, },
        { 0x34f19dc1cc9234f1ULL, 0x9dc1cc9234f19dc1ULL, },
        { 0x742471342bc42c39ULL, 0x3f6a22fd371d7990ULL, },    /*  64  */
        { 0xd4044ee4444e4413ULL, 0x68a71195331b4430ULL, },
        { 0x80a423cc6c264e27ULL, 0x62556624be531a60ULL, },
        { 0x5c36512021725e8aULL, 0x8a465528c764a2e0ULL, },
        { 0xd4044ee4444e4413ULL, 0x68a71195331b4430ULL, },
        { 0x831d26496b929af1ULL, 0xef958b3d113a1254ULL, },
        { 0xeb7041beae82700dULL, 0xd326aa88189c1f8aULL, },
        { 0xa8721dc73869b21eULL, 0xf27179481e1be5e4ULL, },
        { 0x80a423cc6c264e27ULL, 0x62556624be531a60ULL, },    /*  72  */
        { 0xeb7041beae82700dULL, 0xd326aa88189c1f8aULL, },
        { 0x9334e7282d128b79ULL, 0xbc319725797206e9ULL, },
        { 0x670642166b8da1b6ULL, 0xe0d340587bf92d2aULL, },
        { 0x5c36512021725e8aULL, 0x8a465528c764a2e0ULL, },
        { 0xa8721dc73869b21eULL, 0xf27179481e1be5e4ULL, },
        { 0x670642166b8da1b6ULL, 0xe0d340587bf92d2aULL, },
        { 0x4961190d2be5df48ULL, 0x308afe8080952b84ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_DOTP_U_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DOTP_U_H(b128_random[i], b128_random[j],
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
