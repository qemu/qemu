/*
 *  Test program for MSA instruction DOTP_S.H
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
    char *instruction_name =  "DOTP_S.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0002000200020002ULL, 0x0002000200020002ULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x00ac00ac00ac00acULL, 0x00ac00ac00ac00acULL, },
        { 0xff56ff56ff56ff56ULL, 0xff56ff56ff56ff56ULL, },
        { 0x0068006800680068ULL, 0x0068006800680068ULL, },
        { 0xff9aff9aff9aff9aULL, 0xff9aff9aff9aff9aULL, },
        { 0x008fffe5003a008fULL, 0xffe5003a008fffe5ULL, },
        { 0xff73001dffc8ff73ULL, 0x001dffc8ff73001dULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x00ac00ac00ac00acULL, 0x00ac00ac00ac00acULL, },    /*  16  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x39c839c839c839c8ULL, 0x39c839c839c839c8ULL, },
        { 0xc6e4c6e4c6e4c6e4ULL, 0xc6e4c6e4c6e4c6e4ULL, },
        { 0x22f022f022f022f0ULL, 0x22f022f022f022f0ULL, },
        { 0xddbcddbcddbcddbcULL, 0xddbcddbcddbcddbcULL, },
        { 0x300af6ee137c300aULL, 0xf6ee137c300af6eeULL, },
        { 0xd0a209beed30d0a2ULL, 0x09beed30d0a209beULL, },
        { 0xff56ff56ff56ff56ULL, 0xff56ff56ff56ff56ULL, },    /*  24  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xc6e4c6e4c6e4c6e4ULL, 0xc6e4c6e4c6e4c6e4ULL, },
        { 0x3872387238723872ULL, 0x3872387238723872ULL, },
        { 0xdd78dd78dd78dd78ULL, 0xdd78dd78dd78dd78ULL, },
        { 0x21de21de21de21deULL, 0x21de21de21de21deULL, },
        { 0xd08508f7ecbed085ULL, 0x08f7ecbed08508f7ULL, },
        { 0x2ed1f65f12982ed1ULL, 0xf65f12982ed1f65fULL, },
        { 0x0068006800680068ULL, 0x0068006800680068ULL, },    /*  32  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x22f022f022f022f0ULL, 0x22f022f022f022f0ULL, },
        { 0xdd78dd78dd78dd78ULL, 0xdd78dd78dd78dd78ULL, },
        { 0x1520152015201520ULL, 0x1520152015201520ULL, },
        { 0xeb48eb48eb48eb48ULL, 0xeb48eb48eb48eb48ULL, },
        { 0x1d0cfa840bc81d0cULL, 0xfa840bc81d0cfa84ULL, },
        { 0xe35c05e4f4a0e35cULL, 0x05e4f4a0e35c05e4ULL, },
        { 0xff9aff9aff9aff9aULL, 0xff9aff9aff9aff9aULL, },    /*  40  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xddbcddbcddbcddbcULL, 0xddbcddbcddbcddbcULL, },
        { 0x21de21de21de21deULL, 0x21de21de21de21deULL, },
        { 0xeb48eb48eb48eb48ULL, 0xeb48eb48eb48eb48ULL, },
        { 0x1452145214521452ULL, 0x1452145214521452ULL, },
        { 0xe3830561f472e383ULL, 0x0561f472e3830561ULL, },
        { 0x1c17fa390b281c17ULL, 0xfa390b281c17fa39ULL, },
        { 0x008fffe5003a008fULL, 0xffe5003a008fffe5ULL, },    /*  48  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x300af6ee137c300aULL, 0xf6ee137c300af6eeULL, },
        { 0xd08508f7ecbed085ULL, 0x08f7ecbed08508f7ULL, },
        { 0x1d0cfa840bc81d0cULL, 0xfa840bc81d0cfa84ULL, },
        { 0xe3830561f472e383ULL, 0x0561f472e3830561ULL, },
        { 0x360d0f893f04360dULL, 0x0f893f04360d0f89ULL, },
        { 0xca82f05cc136ca82ULL, 0xf05cc136ca82f05cULL, },
        { 0xff73001dffc8ff73ULL, 0x001dffc8ff73001dULL, },    /*  56  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xd0a209beed30d0a2ULL, 0x09beed30d0a209beULL, },
        { 0x2ed1f65f12982ed1ULL, 0xf65f12982ed1f65fULL, },
        { 0xe35c05e4f4a0e35cULL, 0x05e4f4a0e35c05e4ULL, },
        { 0x1c17fa390b281c17ULL, 0xfa390b281c17fa39ULL, },
        { 0xca82f05cc136ca82ULL, 0xf05cc136ca82f05cULL, },
        { 0x34f10fc13e9234f1ULL, 0x0fc13e9234f10fc1ULL, },
        { 0x64240d342bc42c39ULL, 0x3f6a22fd3b1d1990ULL, },    /*  64  */
        { 0xe704ebe4e24eef13ULL, 0x01a706951e1be630ULL, },
        { 0x4ca419cce226b927ULL, 0xfb55fd241553f560ULL, },
        { 0xec36ee202172098aULL, 0xd846ec28206404e0ULL, },
        { 0xe704ebe4e24eef13ULL, 0x01a706951e1be630ULL, },
        { 0x111d264945920cf1ULL, 0x0195153d113a1a54ULL, },
        { 0xea70debeff82160dULL, 0x04260f88039c0b8aULL, },
        { 0xe9721dc70769091eULL, 0xf8711c48091bf7e4ULL, },
        { 0x4ca419cce226b927ULL, 0xfb55fd241553f560ULL, },    /*  72  */
        { 0xea70debeff82160dULL, 0x04260f88039c0b8aULL, },
        { 0x3b3437281d127579ULL, 0x0c310d25237206e9ULL, },
        { 0xf706df16dc8de6b6ULL, 0xf0d31b5827f9f42aULL, },
        { 0xec36ee202172098aULL, 0xd846ec28206404e0ULL, },
        { 0xe9721dc70769091eULL, 0xf8711c48091bf7e4ULL, },
        { 0xf706df16dc8de6b6ULL, 0xf0d31b5827f9f42aULL, },
        { 0x4961190d2be51b48ULL, 0x348a3e802e952784ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_DOTP_S_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DOTP_S_H(b128_random[i], b128_random[j],
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
