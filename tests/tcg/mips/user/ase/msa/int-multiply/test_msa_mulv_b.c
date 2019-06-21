/*
 *  Test program for MSA instruction MULV.B
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
    char *instruction_name =  "MULV.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5656565656565656ULL, 0x5656565656565656ULL, },
        { 0xababababababababULL, 0xababababababababULL, },
        { 0x3434343434343434ULL, 0x3434343434343434ULL, },
        { 0xcdcdcdcdcdcdcdcdULL, 0xcdcdcdcdcdcdcdcdULL, },
        { 0x1d72c81d72c81d72ULL, 0xc81d72c81d72c81dULL, },
        { 0xe48f39e48f39e48fULL, 0x39e48f39e48f39e4ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5656565656565656ULL, 0x5656565656565656ULL, },    /*  16  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xe4e4e4e4e4e4e4e4ULL, 0xe4e4e4e4e4e4e4e4ULL, },
        { 0x7272727272727272ULL, 0x7272727272727272ULL, },
        { 0x7878787878787878ULL, 0x7878787878787878ULL, },
        { 0xdedededededededeULL, 0xdedededededededeULL, },
        { 0xbe4c30be4c30be4cULL, 0x30be4c30be4c30beULL, },
        { 0x980a26980a26980aULL, 0x26980a26980a2698ULL, },
        { 0xababababababababULL, 0xababababababababULL, },    /*  24  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x7272727272727272ULL, 0x7272727272727272ULL, },
        { 0x3939393939393939ULL, 0x3939393939393939ULL, },
        { 0xbcbcbcbcbcbcbcbcULL, 0xbcbcbcbcbcbcbcbcULL, },
        { 0xefefefefefefefefULL, 0xefefefefefefefefULL, },
        { 0x5f26985f26985f26ULL, 0x985f26985f26985fULL, },
        { 0x4c85134c85134c85ULL, 0x134c85134c85134cULL, },
        { 0x3434343434343434ULL, 0x3434343434343434ULL, },    /*  32  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x7878787878787878ULL, 0x7878787878787878ULL, },
        { 0xbcbcbcbcbcbcbcbcULL, 0xbcbcbcbcbcbcbcbcULL, },
        { 0x9090909090909090ULL, 0x9090909090909090ULL, },
        { 0xa4a4a4a4a4a4a4a4ULL, 0xa4a4a4a4a4a4a4a4ULL, },
        { 0xe428a0e428a0e428ULL, 0xa0e428a0e428a0e4ULL, },
        { 0x500c94500c94500cULL, 0x94500c94500c9450ULL, },
        { 0xcdcdcdcdcdcdcdcdULL, 0xcdcdcdcdcdcdcdcdULL, },    /*  40  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xdedededededededeULL, 0xdedededededededeULL, },
        { 0xefefefefefefefefULL, 0xefefefefefefefefULL, },
        { 0xa4a4a4a4a4a4a4a4ULL, 0xa4a4a4a4a4a4a4a4ULL, },
        { 0x2929292929292929ULL, 0x2929292929292929ULL, },
        { 0x394a28394a28394aULL, 0x28394a28394a2839ULL, },
        { 0x9483a59483a59483ULL, 0xa59483a59483a594ULL, },
        { 0x1d72c81d72c81d72ULL, 0xc81d72c81d72c81dULL, },    /*  48  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xbe4c30be4c30be4cULL, 0x30be4c30be4c30beULL, },
        { 0x5f26985f26985f26ULL, 0x985f26985f26985fULL, },
        { 0xe428a0e428a0e428ULL, 0xa0e428a0e428a0e4ULL, },
        { 0x394a28394a28394aULL, 0x28394a28394a2839ULL, },
        { 0x49c44049c44049c4ULL, 0x4049c44049c44049ULL, },
        { 0xd4ae88d4ae88d4aeULL, 0x88d4ae88d4ae88d4ULL, },
        { 0xe48f39e48f39e48fULL, 0x39e48f39e48f39e4ULL, },    /*  56  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x980a26980a26980aULL, 0x26980a26980a2698ULL, },
        { 0x4c85134c85134c85ULL, 0x134c85134c85134cULL, },
        { 0x500c94500c94500cULL, 0x94500c94500c9450ULL, },
        { 0x9483a59483a59483ULL, 0xa59483a59483a594ULL, },
        { 0xd4ae88d4ae88d4aeULL, 0x88d4ae88d4ae88d4ULL, },
        { 0x10e1b110e1b110e1ULL, 0xb110e1b110e1b110ULL, },
        { 0x40e4a49040843900ULL, 0xf971798404190090ULL, },    /*  64  */
        { 0x58ac00e408461300ULL, 0x4661098cd64560d0ULL, },
        { 0x60445478e83e2700ULL, 0x6de882a2aaa970f0ULL, },
        { 0x80b6c45cb0c20a80ULL, 0x4ff7d850aeb66080ULL, },
        { 0x58ac00e408461300ULL, 0x4661098cd64560d0ULL, },
        { 0x190400492969b140ULL, 0x445199a4b9814410ULL, },
        { 0xa4cc00bea5dd0d00ULL, 0xbe68a2e60795dab0ULL, },
        { 0xd0a200c74623ae70ULL, 0xea8758f0dd3e6480ULL, },
        { 0x60445478e83e2700ULL, 0x6de882a2aaa970f0ULL, },    /*  72  */
        { 0xa4cc00bea5dd0d00ULL, 0xbe68a2e60795dab0ULL, },
        { 0x90a444e4b1617900ULL, 0xf140240139395990ULL, },
        { 0x40c6f422ee9fb600ULL, 0x7b583028e316aa80ULL, },
        { 0x80b6c45cb0c20a80ULL, 0x4ff7d850aeb66080ULL, },
        { 0xd0a200c74623ae70ULL, 0xea8758f0dd3e6480ULL, },
        { 0x40c6f422ee9fb600ULL, 0x7b583028e316aa80ULL, },
        { 0x0061e429846184c4ULL, 0xa9e1404091048400ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MULV_B(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MULV_B(b128_random[i], b128_random[j],
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
