/*
 *  Test program for MSA instruction DIV_U.B
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
    char *group_name = "Int Divide";
    char *instruction_name =  "DIV_U.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0x0303030303030303ULL, 0x0303030303030303ULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0x0505050505050505ULL, 0x0505050505050505ULL, },
        { 0x0101040101040101ULL, 0x0401010401010401ULL, },
        { 0x0902010902010902ULL, 0x0109020109020109ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  16  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0x0202020202020202ULL, 0x0202020202020202ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0303030303030303ULL, 0x0303030303030303ULL, },
        { 0x0001030001030001ULL, 0x0300010300010300ULL, },
        { 0x0601000601000601ULL, 0x0006010006010006ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  24  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0x0000010000010000ULL, 0x0100000100000100ULL, },
        { 0x0300000300000300ULL, 0x0003000003000003ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  32  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0x0202020202020202ULL, 0x0202020202020202ULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0x0404040404040404ULL, 0x0404040404040404ULL, },
        { 0x0001030001030001ULL, 0x0300010300010300ULL, },
        { 0x0701010701010701ULL, 0x0107010107010107ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  40  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0100000100000100ULL, 0x0001000001000001ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  48  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0100000100000100ULL, 0x0001000001000001ULL, },
        { 0x0201000201000201ULL, 0x0002010002010002ULL, },
        { 0x0100000100000100ULL, 0x0001000001000001ULL, },
        { 0x0402010402010402ULL, 0x0104020104020104ULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0x0801000801000801ULL, 0x0008010008010008ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  56  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000010000010000ULL, 0x0100000100000100ULL, },
        { 0x0001020001020001ULL, 0x0200010200010200ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0002030002030002ULL, 0x0300020300020300ULL, },
        { 0x0000030000030000ULL, 0x0300000300000300ULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },    /*  64  */
        { 0x0000ff0200000008ULL, 0x040000030c010200ULL, },
        { 0x0001010100000000ULL, 0x0100000001020400ULL, },
        { 0x01010a0200020000ULL, 0x0000000001010000ULL, },
        { 0x0101000001010200ULL, 0x0002110000000015ULL, },
        { 0x0101ff0101010101ULL, 0x0101010101010101ULL, },
        { 0x0102000000000100ULL, 0x000100000001020cULL, },
        { 0x0202000100030000ULL, 0x0001010000000001ULL, },
        { 0x0100000004020102ULL, 0x0002120200000001ULL, },    /*  72  */
        { 0x0000ff0102010010ULL, 0x0200010908000000ULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0x0101070201040001ULL, 0x0000010101000000ULL, },
        { 0x0000000002000201ULL, 0x01020c020000010dULL, },
        { 0x0000ff0001000109ULL, 0x0700000808010200ULL, },
        { 0x0000000000000100ULL, 0x0301000000010608ULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_DIV_U_B(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DIV_U_B(b128_random[i], b128_random[j],
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
