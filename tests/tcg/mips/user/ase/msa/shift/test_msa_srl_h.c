/*
 *  Test program for MSA instruction SRL.H
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
    char *instruction_name =  "SRL.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x003f003f003f003fULL, 0x003f003f003f003fULL, },
        { 0x07ff07ff07ff07ffULL, 0x07ff07ff07ff07ffULL, },
        { 0x000f000f000f000fULL, 0x000f000f000f000fULL, },
        { 0x1fff1fff1fff1fffULL, 0x1fff1fff1fff1fffULL, },
        { 0x00031fff00ff0003ULL, 0x1fff00ff00031fffULL, },
        { 0x7fff000f01ff7fffULL, 0x000f01ff7fff000fULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x002a002a002a002aULL, 0x002a002a002a002aULL, },
        { 0x0555055505550555ULL, 0x0555055505550555ULL, },
        { 0x000a000a000a000aULL, 0x000a000a000a000aULL, },
        { 0x1555155515551555ULL, 0x1555155515551555ULL, },
        { 0x0002155500aa0002ULL, 0x155500aa00021555ULL, },
        { 0x5555000a01555555ULL, 0x000a01555555000aULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x0015001500150015ULL, 0x0015001500150015ULL, },
        { 0x02aa02aa02aa02aaULL, 0x02aa02aa02aa02aaULL, },
        { 0x0005000500050005ULL, 0x0005000500050005ULL, },
        { 0x0aaa0aaa0aaa0aaaULL, 0x0aaa0aaa0aaa0aaaULL, },
        { 0x00010aaa00550001ULL, 0x0aaa005500010aaaULL, },
        { 0x2aaa000500aa2aaaULL, 0x000500aa2aaa0005ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x0033003300330033ULL, 0x0033003300330033ULL, },
        { 0x0666066606660666ULL, 0x0666066606660666ULL, },
        { 0x000c000c000c000cULL, 0x000c000c000c000cULL, },
        { 0x1999199919991999ULL, 0x1999199919991999ULL, },
        { 0x0003199900cc0003ULL, 0x199900cc00031999ULL, },
        { 0x6666000c01996666ULL, 0x000c01996666000cULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x000c000c000c000cULL, 0x000c000c000c000cULL, },
        { 0x0199019901990199ULL, 0x0199019901990199ULL, },
        { 0x0003000300030003ULL, 0x0003000300030003ULL, },
        { 0x0666066606660666ULL, 0x0666066606660666ULL, },
        { 0x0000066600330000ULL, 0x0666003300000666ULL, },
        { 0x1999000300661999ULL, 0x0003006619990003ULL, },
        { 0x0001000000010001ULL, 0x0000000100010000ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x0038000e00230038ULL, 0x000e00230038000eULL, },
        { 0x071c01c70471071cULL, 0x01c70471071c01c7ULL, },
        { 0x000e00030008000eULL, 0x00030008000e0003ULL, },
        { 0x1c71071c11c71c71ULL, 0x071c11c71c71071cULL, },
        { 0x0003071c008e0003ULL, 0x071c008e0003071cULL, },
        { 0x71c70003011c71c7ULL, 0x0003011c71c70003ULL, },
        { 0x0000000100000000ULL, 0x0001000000000001ULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x00070031001c0007ULL, 0x0031001c00070031ULL, },
        { 0x00e30638038e00e3ULL, 0x0638038e00e30638ULL, },
        { 0x0001000c00070001ULL, 0x000c00070001000cULL, },
        { 0x038e18e30e38038eULL, 0x18e30e38038e18e3ULL, },
        { 0x000018e300710000ULL, 0x18e30071000018e3ULL, },
        { 0x0e38000c00e30e38ULL, 0x000c00e30e38000cULL, },
        { 0x0022000e0a185540ULL, 0x00960000001f000bULL, },    /*  64  */
        { 0x00021cd9050c0055ULL, 0x009600020001000bULL, },
        { 0x0022003900005540ULL, 0x004b0000001f0b00ULL, },
        { 0x0001000714310001ULL, 0x25b3000b3f9eb00cULL, },
        { 0x003e00001364c708ULL, 0x0025000200020005ULL, },
        { 0x0003000c09b200c7ULL, 0x0025002e00000005ULL, },
        { 0x003e00000000c708ULL, 0x001200010002052fULL, },
        { 0x0001000026c90003ULL, 0x097b00bb054f52fcULL, },
        { 0x002b000a2e738b80ULL, 0x004f000300150002ULL, },    /*  72  */
        { 0x000215d51739008bULL, 0x004f003100010002ULL, },
        { 0x002b002b00018b80ULL, 0x0027000100150251ULL, },
        { 0x000100055ce70002ULL, 0x13ec00c62aca2514ULL, },
        { 0x001c0001178ce24eULL, 0x011b00020015000eULL, },
        { 0x000102c90bc600e2ULL, 0x011b00220001000eULL, },
        { 0x001c00050000e24eULL, 0x008d000100150e2aULL, },
        { 0x000000002f180003ULL, 0x46f800882a50e2a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRL_H(b128_pattern[i], b128_pattern[j],
                         b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRL_H(b128_random[i], b128_random[j],
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
