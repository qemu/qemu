/*
 *  Test program for MSA instruction SRA.H
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
    char *instruction_name =  "SRA.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
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
        { 0xffeaffeaffeaffeaULL, 0xffeaffeaffeaffeaULL, },
        { 0xfd55fd55fd55fd55ULL, 0xfd55fd55fd55fd55ULL, },
        { 0xfffafffafffafffaULL, 0xfffafffafffafffaULL, },
        { 0xf555f555f555f555ULL, 0xf555f555f555f555ULL, },
        { 0xfffef555ffaafffeULL, 0xf555ffaafffef555ULL, },
        { 0xd555fffaff55d555ULL, 0xfffaff55d555fffaULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x0015001500150015ULL, 0x0015001500150015ULL, },
        { 0x02aa02aa02aa02aaULL, 0x02aa02aa02aa02aaULL, },
        { 0x0005000500050005ULL, 0x0005000500050005ULL, },
        { 0x0aaa0aaa0aaa0aaaULL, 0x0aaa0aaa0aaa0aaaULL, },
        { 0x00010aaa00550001ULL, 0x0aaa005500010aaaULL, },
        { 0x2aaa000500aa2aaaULL, 0x000500aa2aaa0005ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xfff3fff3fff3fff3ULL, 0xfff3fff3fff3fff3ULL, },
        { 0xfe66fe66fe66fe66ULL, 0xfe66fe66fe66fe66ULL, },
        { 0xfffcfffcfffcfffcULL, 0xfffcfffcfffcfffcULL, },
        { 0xf999f999f999f999ULL, 0xf999f999f999f999ULL, },
        { 0xfffff999ffccffffULL, 0xf999ffccfffff999ULL, },
        { 0xe666fffcff99e666ULL, 0xfffcff99e666fffcULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x000c000c000c000cULL, 0x000c000c000c000cULL, },
        { 0x0199019901990199ULL, 0x0199019901990199ULL, },
        { 0x0003000300030003ULL, 0x0003000300030003ULL, },
        { 0x0666066606660666ULL, 0x0666066606660666ULL, },
        { 0x0000066600330000ULL, 0x0666003300000666ULL, },
        { 0x1999000300661999ULL, 0x0003006619990003ULL, },
        { 0xffff0000ffffffffULL, 0x0000ffffffff0000ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xfff8000effe3fff8ULL, 0x000effe3fff8000eULL, },
        { 0xff1c01c7fc71ff1cULL, 0x01c7fc71ff1c01c7ULL, },
        { 0xfffe0003fff8fffeULL, 0x0003fff8fffe0003ULL, },
        { 0xfc71071cf1c7fc71ULL, 0x071cf1c7fc71071cULL, },
        { 0xffff071cff8effffULL, 0x071cff8effff071cULL, },
        { 0xf1c70003ff1cf1c7ULL, 0x0003ff1cf1c70003ULL, },
        { 0x0000ffff00000000ULL, 0xffff00000000ffffULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x0007fff1001c0007ULL, 0xfff1001c0007fff1ULL, },
        { 0x00e3fe38038e00e3ULL, 0xfe38038e00e3fe38ULL, },
        { 0x0001fffc00070001ULL, 0xfffc00070001fffcULL, },
        { 0x038ef8e30e38038eULL, 0xf8e30e38038ef8e3ULL, },
        { 0x0000f8e300710000ULL, 0xf8e300710000f8e3ULL, },
        { 0x0e38fffc00e30e38ULL, 0xfffc00e30e38fffcULL, },
        { 0xffe2fffe0a185540ULL, 0x00960000fffffffbULL, },    /*  64  */
        { 0xfffefcd9050c0055ULL, 0x00960002fffffffbULL, },
        { 0xffe2fff900005540ULL, 0x004b0000fffffb00ULL, },
        { 0xffffffff14310001ULL, 0x25b3000bff9eb00cULL, },
        { 0xfffe00001364c708ULL, 0x0025fffe00020005ULL, },
        { 0xffff000c09b2ffc7ULL, 0x0025ffee00000005ULL, },
        { 0xfffe00000000c708ULL, 0x0012ffff0002052fULL, },
        { 0xffff000026c9ffffULL, 0x097bffbb054f52fcULL, },
        { 0xffebfffaee738b80ULL, 0x004ffffffff50002ULL, },    /*  72  */
        { 0xfffef5d5f739ff8bULL, 0x004ffff1ffff0002ULL, },
        { 0xffebffebffff8b80ULL, 0x0027fffffff50251ULL, },
        { 0xfffffffddce7fffeULL, 0x13ecffc6eaca2514ULL, },
        { 0x001c0001178ce24eULL, 0xff1bfffefff5fffeULL, },
        { 0x000102c90bc6ffe2ULL, 0xff1bffe2fffffffeULL, },
        { 0x001c00050000e24eULL, 0xff8dfffffff5fe2aULL, },
        { 0x000000002f18ffffULL, 0xc6f8ff88ea50e2a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRA_H(b128_pattern[i], b128_pattern[j],
                         b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRA_H(b128_random[i], b128_random[j],
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
