/*
 *  Test program for MSA instruction SUBS_S.B
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
    char *instruction_name =  "SUBS_S.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5656565656565656ULL, 0x5656565656565656ULL, },
        { 0xababababababababULL, 0xababababababababULL, },
        { 0x3434343434343434ULL, 0x3434343434343434ULL, },
        { 0xcdcdcdcdcdcdcdcdULL, 0xcdcdcdcdcdcdcdcdULL, },
        { 0x1d72c81d72c81d72ULL, 0xc81d72c81d72c81dULL, },
        { 0xe48f39e48f39e48fULL, 0x39e48f39e48f39e4ULL, },
        { 0xababababababababULL, 0xababababababababULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },
        { 0xdedededededededeULL, 0xdedededededededeULL, },
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },
        { 0xc71c80c71c80c71cULL, 0x80c71c80c71c80c7ULL, },
        { 0x8e80e38e80e38e80ULL, 0xe38e80e38e80e38eULL, },
        { 0x5656565656565656ULL, 0x5656565656565656ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x727f1d727f1d727fULL, 0x1d727f1d727f1d72ULL, },
        { 0x39e47f39e47f39e4ULL, 0x7f39e47f39e47f39ULL, },
        { 0xcdcdcdcdcdcdcdcdULL, 0xcdcdcdcdcdcdcdcdULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x9999999999999999ULL, 0x9999999999999999ULL, },
        { 0xe93e94e93e94e93eULL, 0x94e93e94e93e94e9ULL, },
        { 0xb08005b08005b080ULL, 0x05b08005b08005b0ULL, },
        { 0x3434343434343434ULL, 0x3434343434343434ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },
        { 0xdedededededededeULL, 0xdedededededededeULL, },
        { 0x6767676767676767ULL, 0x6767676767676767ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x507ffb507ffb507fULL, 0xfb507ffb507ffb50ULL, },
        { 0x17c26c17c26c17c2ULL, 0x6c17c26c17c26c17ULL, },
        { 0xe48f39e48f39e48fULL, 0x39e48f39e48f39e4ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x39e47f39e47f39e4ULL, 0x7f39e47f39e47f39ULL, },
        { 0x8e80e38e80e38e80ULL, 0xe38e80e38e80e38eULL, },
        { 0x17c26c17c26c17c2ULL, 0x6c17c26c17c26c17ULL, },
        { 0xb08005b08005b080ULL, 0x05b08005b08005b0ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xc78071c78071c780ULL, 0x71c78071c78071c7ULL, },
        { 0x1d72c81d72c81d72ULL, 0xc81d72c81d72c81dULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x727f1d727f1d727fULL, 0x1d727f1d727f1d72ULL, },
        { 0xc71c80c71c80c71cULL, 0x80c71c80c71c80c7ULL, },
        { 0x507ffb507ffb507fULL, 0xfb507ffb507ffb50ULL, },
        { 0xe93e94e93e94e93eULL, 0x94e93e94e93e94e9ULL, },
        { 0x397f8f397f8f397fULL, 0x8f397f8f397f8f39ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  64  */
        { 0x8d7fe680db7f7f38ULL, 0x39705044e93c8010ULL, },
        { 0xdc1038226f7f7f7fULL, 0x247f455f53508bf8ULL, },
        { 0x801bd080ca3173f2ULL, 0x7f767f7f5539ce6cULL, },
        { 0x73801a7f258080c8ULL, 0xc790b0bc17c47ff0ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x4f80527f7fc43c7fULL, 0xeb1ff51b6a142de8ULL, },
        { 0x8b80ea16ef80e5baULL, 0x7f0633426cfd705cULL, },
        { 0x24f0c8de91808080ULL, 0xdc80bba1adb07508ULL, },    /*  72  */
        { 0xb17fae80803cc480ULL, 0x15e10be596ecd318ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x800b9880809ea980ULL, 0x7fe73e2702e94374ULL, },
        { 0x7fe5307f36cf8d0eULL, 0x808a8080abc73294ULL, },
        { 0x757f16ea117f1b46ULL, 0x80facdbe940390a4ULL, },
        { 0x7ff5687f7f62577fULL, 0x8019c2d9fe17bd8cULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_SUBS_S_B(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_SUBS_S_B(b128_random[i], b128_random[j],
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
