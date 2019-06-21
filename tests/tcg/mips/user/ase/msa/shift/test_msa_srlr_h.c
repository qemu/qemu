/*
 *  Test program for MSA instruction SRLR.H
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
    char *instruction_name =  "SRLR.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0002000200020002ULL, 0x0002000200020002ULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0040004000400040ULL, 0x0040004000400040ULL, },
        { 0x0800080008000800ULL, 0x0800080008000800ULL, },
        { 0x0010001000100010ULL, 0x0010001000100010ULL, },
        { 0x2000200020002000ULL, 0x2000200020002000ULL, },
        { 0x0004200001000004ULL, 0x2000010000042000ULL, },
        { 0x8000001002008000ULL, 0x0010020080000010ULL, },
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
        { 0x002b002b002b002bULL, 0x002b002b002b002bULL, },
        { 0x0555055505550555ULL, 0x0555055505550555ULL, },
        { 0x000b000b000b000bULL, 0x000b000b000b000bULL, },
        { 0x1555155515551555ULL, 0x1555155515551555ULL, },
        { 0x0003155500ab0003ULL, 0x155500ab00031555ULL, },
        { 0x5555000b01555555ULL, 0x000b01555555000bULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x0015001500150015ULL, 0x0015001500150015ULL, },
        { 0x02ab02ab02ab02abULL, 0x02ab02ab02ab02abULL, },
        { 0x0005000500050005ULL, 0x0005000500050005ULL, },
        { 0x0aab0aab0aab0aabULL, 0x0aab0aab0aab0aabULL, },
        { 0x00010aab00550001ULL, 0x0aab005500010aabULL, },
        { 0x2aab000500ab2aabULL, 0x000500ab2aab0005ULL, },
        { 0x0002000200020002ULL, 0x0002000200020002ULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x0033003300330033ULL, 0x0033003300330033ULL, },
        { 0x0666066606660666ULL, 0x0666066606660666ULL, },
        { 0x000d000d000d000dULL, 0x000d000d000d000dULL, },
        { 0x199a199a199a199aULL, 0x199a199a199a199aULL, },
        { 0x0003199a00cd0003ULL, 0x199a00cd0003199aULL, },
        { 0x6666000d019a6666ULL, 0x000d019a6666000dULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x000d000d000d000dULL, 0x000d000d000d000dULL, },
        { 0x019a019a019a019aULL, 0x019a019a019a019aULL, },
        { 0x0003000300030003ULL, 0x0003000300030003ULL, },
        { 0x0666066606660666ULL, 0x0666066606660666ULL, },
        { 0x0001066600330001ULL, 0x0666003300010666ULL, },
        { 0x199a00030066199aULL, 0x00030066199a0003ULL, },
        { 0x0002000000010002ULL, 0x0000000100020000ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x0039000e00240039ULL, 0x000e00240039000eULL, },
        { 0x071c01c70472071cULL, 0x01c70472071c01c7ULL, },
        { 0x000e00040009000eULL, 0x00040009000e0004ULL, },
        { 0x1c72071c11c71c72ULL, 0x071c11c71c72071cULL, },
        { 0x0004071c008e0004ULL, 0x071c008e0004071cULL, },
        { 0x71c70004011c71c7ULL, 0x0004011c71c70004ULL, },
        { 0x0000000200010000ULL, 0x0002000100000002ULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x00070032001c0007ULL, 0x0032001c00070032ULL, },
        { 0x00e40639038e00e4ULL, 0x0639038e00e40639ULL, },
        { 0x0002000c00070002ULL, 0x000c00070002000cULL, },
        { 0x038e18e40e39038eULL, 0x18e40e39038e18e4ULL, },
        { 0x000018e400720000ULL, 0x18e40072000018e4ULL, },
        { 0x0e39000c00e40e39ULL, 0x000c00e40e39000cULL, },
        { 0x0022000e0a195540ULL, 0x009700000020000bULL, },    /*  64  */
        { 0x00021cda050c0055ULL, 0x009700030002000bULL, },
        { 0x0022003a00005540ULL, 0x004b000000200b01ULL, },
        { 0x0001000714310001ULL, 0x25b4000b3f9fb00cULL, },
        { 0x003f00001365c708ULL, 0x0026000300030005ULL, },
        { 0x0004000c09b200c7ULL, 0x0026002f00000005ULL, },
        { 0x003f00000001c708ULL, 0x0013000100030530ULL, },
        { 0x0002000026ca0003ULL, 0x097c00bb055052fcULL, },
        { 0x002b000b2e748b80ULL, 0x0050000300150002ULL, },    /*  72  */
        { 0x000315d5173a008cULL, 0x0050003200010002ULL, },
        { 0x002b002c00018b80ULL, 0x0028000200150251ULL, },
        { 0x000100055ce80002ULL, 0x13ec00c72acb2514ULL, },
        { 0x001c0001178ce24eULL, 0x011c00020015000eULL, },
        { 0x000202ca0bc600e2ULL, 0x011c00220001000eULL, },
        { 0x001c00060001e24eULL, 0x008e000100150e2aULL, },
        { 0x000100012f190004ULL, 0x46f900892a51e2a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRLR_H(b128_pattern[i], b128_pattern[j],
                          b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRLR_H(b128_random[i], b128_random[j],
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
