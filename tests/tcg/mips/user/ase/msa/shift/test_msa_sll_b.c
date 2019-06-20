/*
 *  Test program for MSA instruction SLL.B
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
    char *group_name = "Shift";
    char *instruction_name =  "SLL.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xfcfcfcfcfcfcfcfcULL, 0xfcfcfcfcfcfcfcfcULL, },
        { 0xe0e0e0e0e0e0e0e0ULL, 0xe0e0e0e0e0e0e0e0ULL, },
        { 0xf0f0f0f0f0f0f0f0ULL, 0xf0f0f0f0f0f0f0f0ULL, },
        { 0xf8f8f8f8f8f8f8f8ULL, 0xf8f8f8f8f8f8f8f8ULL, },
        { 0xf8c0fff8c0fff8c0ULL, 0xfff8c0fff8c0fff8ULL, },
        { 0xf0fe80f0fe80f0feULL, 0x80f0fe80f0fe80f0ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xa8a8a8a8a8a8a8a8ULL, 0xa8a8a8a8a8a8a8a8ULL, },
        { 0x4040404040404040ULL, 0x4040404040404040ULL, },
        { 0xa0a0a0a0a0a0a0a0ULL, 0xa0a0a0a0a0a0a0a0ULL, },
        { 0x5050505050505050ULL, 0x5050505050505050ULL, },
        { 0x5080aa5080aa5080ULL, 0xaa5080aa5080aa50ULL, },
        { 0xa05400a05400a054ULL, 0x00a05400a05400a0ULL, },
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5454545454545454ULL, 0x5454545454545454ULL, },
        { 0xa0a0a0a0a0a0a0a0ULL, 0xa0a0a0a0a0a0a0a0ULL, },
        { 0x5050505050505050ULL, 0x5050505050505050ULL, },
        { 0xa8a8a8a8a8a8a8a8ULL, 0xa8a8a8a8a8a8a8a8ULL, },
        { 0xa84055a84055a840ULL, 0x55a84055a84055a8ULL, },
        { 0x50aa8050aa8050aaULL, 0x8050aa8050aa8050ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x3030303030303030ULL, 0x3030303030303030ULL, },
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },
        { 0xc0c0c0c0c0c0c0c0ULL, 0xc0c0c0c0c0c0c0c0ULL, },
        { 0x6060606060606060ULL, 0x6060606060606060ULL, },
        { 0x6000cc6000cc6000ULL, 0xcc6000cc6000cc60ULL, },
        { 0xc09800c09800c098ULL, 0x00c09800c09800c0ULL, },
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x6060606060606060ULL, 0x6060606060606060ULL, },
        { 0x3030303030303030ULL, 0x3030303030303030ULL, },
        { 0x9898989898989898ULL, 0x9898989898989898ULL, },
        { 0x98c03398c03398c0ULL, 0x3398c03398c03398ULL, },
        { 0x3066803066803066ULL, 0x8030668030668030ULL, },
        { 0x8000008000008000ULL, 0x0080000080000080ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x8c38e08c38e08c38ULL, 0xe08c38e08c38e08cULL, },
        { 0x60c00060c00060c0ULL, 0x0060c00060c00060ULL, },
        { 0x30e08030e08030e0ULL, 0x8030e08030e08030ULL, },
        { 0x1870c01870c01870ULL, 0xc01870c01870c018ULL, },
        { 0x1880381880381880ULL, 0x3818803818803818ULL, },
        { 0x301c00301c00301cULL, 0x00301c00301c0030ULL, },
        { 0x0080800080800080ULL, 0x8000808000808000ULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x70c41c70c41c70c4ULL, 0x1c70c41c70c41c70ULL, },
        { 0x8020e08020e08020ULL, 0xe08020e08020e080ULL, },
        { 0xc01070c01070c010ULL, 0x70c01070c01070c0ULL, },
        { 0xe08838e08838e088ULL, 0x38e08838e08838e0ULL, },
        { 0xe040c7e040c7e040ULL, 0xc7e040c7e040c7e0ULL, },
        { 0xc0e280c0e280c0e2ULL, 0x80c0e280c0e280c0ULL, },
        { 0x88a880c02888a040ULL, 0x5880588080d8b0c0ULL, },    /*  64  */
        { 0x4080e66000108040ULL, 0x2c805878c080c0c0ULL, },
        { 0x80a880305000a840ULL, 0x8067c000f0d800c0ULL, },
        { 0x8800808000c45400ULL, 0x60ce0b5efcecc00cULL, },
        { 0xfbf800304d4ce008ULL, 0x9080d88040f852c0ULL, },
        { 0xd8800018a0988008ULL, 0x4880d868a08048c0ULL, },
        { 0xb0f8008c9a803808ULL, 0x00f7c000a8f840c0ULL, },
        { 0xfb00006040261c00ULL, 0x40eebb1a2afc48fcULL, },
        { 0xac6880a0b93c6080ULL, 0x380030c0c0582540ULL, },    /*  72  */
        { 0x6080ae5020788080ULL, 0x9c0030fc60809440ULL, },
        { 0xc06880a872805880ULL, 0x80d880805858a040ULL, },
        { 0xac008040409e2c00ULL, 0xe0b0c6ff56ac9414ULL, },
        { 0x703c80d05ec4404eULL, 0x688040004010e200ULL, },
        { 0x80c01668c088004eULL, 0x3480406020008800ULL, },
        { 0x003c8034bc80104eULL, 0x80f1000048104000ULL, },
        { 0x708080a080628880ULL, 0xa0e288d8520888a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_SLL_B(b128_pattern[i], b128_pattern[j],
                         b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_SLL_B(b128_random[i], b128_random[j],
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
