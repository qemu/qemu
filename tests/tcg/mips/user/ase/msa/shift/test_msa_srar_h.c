/*
 *  Test program for MSA instruction SRAR.H
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
    char *instruction_name =  "SRAR.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xffebffebffebffebULL, 0xffebffebffebffebULL, },
        { 0xfd55fd55fd55fd55ULL, 0xfd55fd55fd55fd55ULL, },
        { 0xfffbfffbfffbfffbULL, 0xfffbfffbfffbfffbULL, },
        { 0xf555f555f555f555ULL, 0xf555f555f555f555ULL, },
        { 0xfffff555ffabffffULL, 0xf555ffabfffff555ULL, },
        { 0xd555fffbff55d555ULL, 0xfffbff55d555fffbULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x0015001500150015ULL, 0x0015001500150015ULL, },
        { 0x02ab02ab02ab02abULL, 0x02ab02ab02ab02abULL, },
        { 0x0005000500050005ULL, 0x0005000500050005ULL, },
        { 0x0aab0aab0aab0aabULL, 0x0aab0aab0aab0aabULL, },
        { 0x00010aab00550001ULL, 0x0aab005500010aabULL, },
        { 0x2aab000500ab2aabULL, 0x000500ab2aab0005ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xfff3fff3fff3fff3ULL, 0xfff3fff3fff3fff3ULL, },
        { 0xfe66fe66fe66fe66ULL, 0xfe66fe66fe66fe66ULL, },
        { 0xfffdfffdfffdfffdULL, 0xfffdfffdfffdfffdULL, },
        { 0xf99af99af99af99aULL, 0xf99af99af99af99aULL, },
        { 0xfffff99affcdffffULL, 0xf99affcdfffff99aULL, },
        { 0xe666fffdff9ae666ULL, 0xfffdff9ae666fffdULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x000d000d000d000dULL, 0x000d000d000d000dULL, },
        { 0x019a019a019a019aULL, 0x019a019a019a019aULL, },
        { 0x0003000300030003ULL, 0x0003000300030003ULL, },
        { 0x0666066606660666ULL, 0x0666066606660666ULL, },
        { 0x0001066600330001ULL, 0x0666003300010666ULL, },
        { 0x199a00030066199aULL, 0x00030066199a0003ULL, },
        { 0x00000000ffff0000ULL, 0x0000ffff00000000ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xfff9000effe4fff9ULL, 0x000effe4fff9000eULL, },
        { 0xff1c01c7fc72ff1cULL, 0x01c7fc72ff1c01c7ULL, },
        { 0xfffe0004fff9fffeULL, 0x0004fff9fffe0004ULL, },
        { 0xfc72071cf1c7fc72ULL, 0x071cf1c7fc72071cULL, },
        { 0x0000071cff8e0000ULL, 0x071cff8e0000071cULL, },
        { 0xf1c70004ff1cf1c7ULL, 0x0004ff1cf1c70004ULL, },
        { 0x0000000000010000ULL, 0x0000000100000000ULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x0007fff2001c0007ULL, 0xfff2001c0007fff2ULL, },
        { 0x00e4fe39038e00e4ULL, 0xfe39038e00e4fe39ULL, },
        { 0x0002fffc00070002ULL, 0xfffc00070002fffcULL, },
        { 0x038ef8e40e39038eULL, 0xf8e40e39038ef8e4ULL, },
        { 0x0000f8e400720000ULL, 0xf8e400720000f8e4ULL, },
        { 0x0e39fffc00e40e39ULL, 0xfffc00e40e39fffcULL, },
        { 0xffe2fffe0a195540ULL, 0x009700000000fffbULL, },    /*  64  */
        { 0xfffefcda050c0055ULL, 0x009700030000fffbULL, },
        { 0xffe2fffa00005540ULL, 0x004b00000000fb01ULL, },
        { 0xffffffff14310001ULL, 0x25b4000bff9fb00cULL, },
        { 0xffff00001365c708ULL, 0x0026ffff00030005ULL, },
        { 0x0000000c09b2ffc7ULL, 0x0026ffef00000005ULL, },
        { 0xffff00000001c708ULL, 0x0013ffff00030530ULL, },
        { 0x0000000026caffffULL, 0x097cffbb055052fcULL, },
        { 0xffebfffbee748b80ULL, 0x0050fffffff50002ULL, },    /*  72  */
        { 0xfffff5d5f73aff8cULL, 0x0050fff2ffff0002ULL, },
        { 0xffebffecffff8b80ULL, 0x00280000fff50251ULL, },
        { 0xfffffffddce8fffeULL, 0x13ecffc7eacb2514ULL, },
        { 0x001c0001178ce24eULL, 0xff1cfffefff5fffeULL, },
        { 0x000202ca0bc6ffe2ULL, 0xff1cffe2fffffffeULL, },
        { 0x001c00060001e24eULL, 0xff8efffffff5fe2aULL, },
        { 0x000100012f190000ULL, 0xc6f9ff89ea51e2a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRAR_H(b128_pattern[i], b128_pattern[j],
                          b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRAR_H(b128_random[i], b128_random[j],
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
