/*
 *  Test program for MSA instruction SLL.W
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
    char *instruction_name =  "SLL.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x8000000080000000ULL, 0x8000000080000000ULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xfffffc00fffffc00ULL, 0xfffffc00fffffc00ULL, },
        { 0xffe00000ffe00000ULL, 0xffe00000ffe00000ULL, },
        { 0xfffff000fffff000ULL, 0xfffff000fffff000ULL, },
        { 0xfff80000fff80000ULL, 0xfff80000fff80000ULL, },
        { 0xfffffff8ffffc000ULL, 0xff000000fffffff8ULL, },
        { 0xf0000000fffe0000ULL, 0xffffff80f0000000ULL, },
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
        { 0xaaaaa800aaaaa800ULL, 0xaaaaa800aaaaa800ULL, },
        { 0x5540000055400000ULL, 0x5540000055400000ULL, },
        { 0xaaaaa000aaaaa000ULL, 0xaaaaa000aaaaa000ULL, },
        { 0x5550000055500000ULL, 0x5550000055500000ULL, },
        { 0x55555550aaaa8000ULL, 0xaa00000055555550ULL, },
        { 0xa000000055540000ULL, 0x55555500a0000000ULL, },
        { 0x8000000080000000ULL, 0x8000000080000000ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555540055555400ULL, 0x5555540055555400ULL, },
        { 0xaaa00000aaa00000ULL, 0xaaa00000aaa00000ULL, },
        { 0x5555500055555000ULL, 0x5555500055555000ULL, },
        { 0xaaa80000aaa80000ULL, 0xaaa80000aaa80000ULL, },
        { 0xaaaaaaa855554000ULL, 0x55000000aaaaaaa8ULL, },
        { 0x50000000aaaa0000ULL, 0xaaaaaa8050000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x3333300033333000ULL, 0x3333300033333000ULL, },
        { 0x9980000099800000ULL, 0x9980000099800000ULL, },
        { 0xccccc000ccccc000ULL, 0xccccc000ccccc000ULL, },
        { 0x6660000066600000ULL, 0x6660000066600000ULL, },
        { 0x6666666033330000ULL, 0xcc00000066666660ULL, },
        { 0xc000000099980000ULL, 0x66666600c0000000ULL, },
        { 0x8000000080000000ULL, 0x8000000080000000ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xcccccc00cccccc00ULL, 0xcccccc00cccccc00ULL, },
        { 0x6660000066600000ULL, 0x6660000066600000ULL, },
        { 0x3333300033333000ULL, 0x3333300033333000ULL, },
        { 0x9998000099980000ULL, 0x9998000099980000ULL, },
        { 0x99999998ccccc000ULL, 0x3300000099999998ULL, },
        { 0x3000000066660000ULL, 0x9999998030000000ULL, },
        { 0x8000000000000000ULL, 0x0000000080000000ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x38e38c00e38e3800ULL, 0x8e38e00038e38c00ULL, },
        { 0x1c60000071c00000ULL, 0xc70000001c600000ULL, },
        { 0xe38e30008e38e000ULL, 0x38e38000e38e3000ULL, },
        { 0xc71800001c700000ULL, 0x71c00000c7180000ULL, },
        { 0x1c71c71838e38000ULL, 0x380000001c71c718ULL, },
        { 0x30000000c71c0000ULL, 0x71c71c0030000000ULL, },
        { 0x0000000080000000ULL, 0x8000000000000000ULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0xc71c70001c71c400ULL, 0x71c71c00c71c7000ULL, },
        { 0xe38000008e200000ULL, 0x38e00000e3800000ULL, },
        { 0x1c71c00071c71000ULL, 0xc71c70001c71c000ULL, },
        { 0x38e00000e3880000ULL, 0x8e38000038e00000ULL, },
        { 0xe38e38e0c71c4000ULL, 0xc7000000e38e38e0ULL, },
        { 0xc000000038e20000ULL, 0x8e38e380c0000000ULL, },
        { 0xae6cc00028625540ULL, 0x80000000bb00c000ULL, },    /*  64  */
        { 0x4357366062554000ULL, 0x78000000c0000000ULL, },
        { 0xab9b300028625540ULL, 0x0000000000c00000ULL, },
        { 0x5cd9800095500000ULL, 0x5e000000fe7bb00cULL, },
        { 0xe00630004d93c708ULL, 0x80000000f52fc000ULL, },
        { 0xddf0031893c70800ULL, 0x68000000c0000000ULL, },
        { 0xf8018c004d93c708ULL, 0x000000002fc00000ULL, },
        { 0xc00c6000f1c20000ULL, 0x1a000000153f52fcULL, },
        { 0xaaeaa000b9cf8b80ULL, 0xc0000000b2514000ULL, },    /*  72  */
        { 0x62d57550cf8b8000ULL, 0xfc00000040000000ULL, },
        { 0x6abaa800b9cf8b80ULL, 0x8000000051400000ULL, },
        { 0x55d54000e2e00000ULL, 0xff000000ab2b2514ULL, },
        { 0xf164d0005e31e24eULL, 0x000000002e2a0000ULL, },
        { 0x8278b26831e24e00ULL, 0x6000000000000000ULL, },
        { 0x3c5934005e31e24eULL, 0x000000002a000000ULL, },
        { 0xe2c9a00078938000ULL, 0xd8000000a942e2a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_SLL_W(b128_pattern[i], b128_pattern[j],
                         b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_SLL_W(b128_random[i], b128_random[j],
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
