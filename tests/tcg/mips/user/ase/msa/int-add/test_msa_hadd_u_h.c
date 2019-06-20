/*
 *  Test program for MSA instruction HADD_U.H
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
    char *instruction_name =  "HADD_U.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x01fe01fe01fe01feULL, 0x01fe01fe01fe01feULL, },    /*   0  */
        { 0x00ff00ff00ff00ffULL, 0x00ff00ff00ff00ffULL, },
        { 0x01a901a901a901a9ULL, 0x01a901a901a901a9ULL, },
        { 0x0154015401540154ULL, 0x0154015401540154ULL, },
        { 0x01cb01cb01cb01cbULL, 0x01cb01cb01cb01cbULL, },
        { 0x0132013201320132ULL, 0x0132013201320132ULL, },
        { 0x018d01e20137018dULL, 0x01e20137018d01e2ULL, },
        { 0x0170011b01c60170ULL, 0x011b01c60170011bULL, },
        { 0x00ff00ff00ff00ffULL, 0x00ff00ff00ff00ffULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x00aa00aa00aa00aaULL, 0x00aa00aa00aa00aaULL, },
        { 0x0055005500550055ULL, 0x0055005500550055ULL, },
        { 0x00cc00cc00cc00ccULL, 0x00cc00cc00cc00ccULL, },
        { 0x0033003300330033ULL, 0x0033003300330033ULL, },
        { 0x008e00e30038008eULL, 0x00e30038008e00e3ULL, },
        { 0x0071001c00c70071ULL, 0x001c00c70071001cULL, },
        { 0x01a901a901a901a9ULL, 0x01a901a901a901a9ULL, },    /*  16  */
        { 0x00aa00aa00aa00aaULL, 0x00aa00aa00aa00aaULL, },
        { 0x0154015401540154ULL, 0x0154015401540154ULL, },
        { 0x00ff00ff00ff00ffULL, 0x00ff00ff00ff00ffULL, },
        { 0x0176017601760176ULL, 0x0176017601760176ULL, },
        { 0x00dd00dd00dd00ddULL, 0x00dd00dd00dd00ddULL, },
        { 0x0138018d00e20138ULL, 0x018d00e20138018dULL, },
        { 0x011b00c60171011bULL, 0x00c60171011b00c6ULL, },
        { 0x0154015401540154ULL, 0x0154015401540154ULL, },    /*  24  */
        { 0x0055005500550055ULL, 0x0055005500550055ULL, },
        { 0x00ff00ff00ff00ffULL, 0x00ff00ff00ff00ffULL, },
        { 0x00aa00aa00aa00aaULL, 0x00aa00aa00aa00aaULL, },
        { 0x0121012101210121ULL, 0x0121012101210121ULL, },
        { 0x0088008800880088ULL, 0x0088008800880088ULL, },
        { 0x00e30138008d00e3ULL, 0x0138008d00e30138ULL, },
        { 0x00c60071011c00c6ULL, 0x0071011c00c60071ULL, },
        { 0x01cb01cb01cb01cbULL, 0x01cb01cb01cb01cbULL, },    /*  32  */
        { 0x00cc00cc00cc00ccULL, 0x00cc00cc00cc00ccULL, },
        { 0x0176017601760176ULL, 0x0176017601760176ULL, },
        { 0x0121012101210121ULL, 0x0121012101210121ULL, },
        { 0x0198019801980198ULL, 0x0198019801980198ULL, },
        { 0x00ff00ff00ff00ffULL, 0x00ff00ff00ff00ffULL, },
        { 0x015a01af0104015aULL, 0x01af0104015a01afULL, },
        { 0x013d00e80193013dULL, 0x00e80193013d00e8ULL, },
        { 0x0132013201320132ULL, 0x0132013201320132ULL, },    /*  40  */
        { 0x0033003300330033ULL, 0x0033003300330033ULL, },
        { 0x00dd00dd00dd00ddULL, 0x00dd00dd00dd00ddULL, },
        { 0x0088008800880088ULL, 0x0088008800880088ULL, },
        { 0x00ff00ff00ff00ffULL, 0x00ff00ff00ff00ffULL, },
        { 0x0066006600660066ULL, 0x0066006600660066ULL, },
        { 0x00c10116006b00c1ULL, 0x0116006b00c10116ULL, },
        { 0x00a4004f00fa00a4ULL, 0x004f00fa00a4004fULL, },
        { 0x01e20137018d01e2ULL, 0x0137018d01e20137ULL, },    /*  48  */
        { 0x00e30038008e00e3ULL, 0x0038008e00e30038ULL, },
        { 0x018d00e20138018dULL, 0x00e20138018d00e2ULL, },
        { 0x0138008d00e30138ULL, 0x008d00e30138008dULL, },
        { 0x01af0104015a01afULL, 0x0104015a01af0104ULL, },
        { 0x0116006b00c10116ULL, 0x006b00c10116006bULL, },
        { 0x0171011b00c60171ULL, 0x011b00c60171011bULL, },
        { 0x0154005401550154ULL, 0x0054015501540054ULL, },
        { 0x011b01c60170011bULL, 0x01c60170011b01c6ULL, },    /*  56  */
        { 0x001c00c70071001cULL, 0x00c70071001c00c7ULL, },
        { 0x00c60171011b00c6ULL, 0x0171011b00c60171ULL, },
        { 0x0071011c00c60071ULL, 0x011c00c60071011cULL, },
        { 0x00e80193013d00e8ULL, 0x0193013d00e80193ULL, },
        { 0x004f00fa00a4004fULL, 0x00fa00a4004f00faULL, },
        { 0x00aa01aa00a900aaULL, 0x01aa00a900aa01aaULL, },
        { 0x008d00e30138008dULL, 0x00e30138008d00e3ULL, },
        { 0x00f201b2008a0095ULL, 0x00b20069017900bcULL, },    /*  64  */
        { 0x0146014900bb005dULL, 0x01420025013d01acULL, },
        { 0x00e2019000f700d5ULL, 0x0123010a012900c4ULL, },
        { 0x00d70133005900a3ULL, 0x013c00e301400150ULL, },
        { 0x016500cc00af0107ULL, 0x007901190090005eULL, },
        { 0x01b9006300e000cfULL, 0x010900d50054014eULL, },
        { 0x015500aa011c0147ULL, 0x00ea01ba00400066ULL, },
        { 0x014a004d007e0115ULL, 0x01030193005700f2ULL, },
        { 0x0116017a011b00cbULL, 0x008e012401260031ULL, },    /*  72  */
        { 0x016a0111014c0093ULL, 0x011e00e000ea0121ULL, },
        { 0x010601580188010bULL, 0x00ff01c500d60039ULL, },
        { 0x00fb00fb00ea00d9ULL, 0x0118019e00ed00c5ULL, },
        { 0x00da00e200c00122ULL, 0x00f400e6012400eeULL, },
        { 0x012e007900f100eaULL, 0x018400a200e801deULL, },
        { 0x00ca00c0012d0162ULL, 0x0165018700d400f6ULL, },
        { 0x00bf0063008f0130ULL, 0x017e016000eb0182ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_HADD_U_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_HADD_U_H(b128_random[i], b128_random[j],
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
