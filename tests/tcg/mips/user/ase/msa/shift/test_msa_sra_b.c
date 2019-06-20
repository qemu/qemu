/*
 *  Test program for MSA instruction SRA.B
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
    char *instruction_name =  "SRA.B";
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
        { 0xeaeaeaeaeaeaeaeaULL, 0xeaeaeaeaeaeaeaeaULL, },
        { 0xfdfdfdfdfdfdfdfdULL, 0xfdfdfdfdfdfdfdfdULL, },
        { 0xfafafafafafafafaULL, 0xfafafafafafafafaULL, },
        { 0xf5f5f5f5f5f5f5f5ULL, 0xf5f5f5f5f5f5f5f5ULL, },
        { 0xf5feaaf5feaaf5feULL, 0xaaf5feaaf5feaaf5ULL, },
        { 0xfad5fffad5fffad5ULL, 0xfffad5fffad5fffaULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x1515151515151515ULL, 0x1515151515151515ULL, },
        { 0x0202020202020202ULL, 0x0202020202020202ULL, },
        { 0x0505050505050505ULL, 0x0505050505050505ULL, },
        { 0x0a0a0a0a0a0a0a0aULL, 0x0a0a0a0a0a0a0a0aULL, },
        { 0x0a01550a01550a01ULL, 0x550a01550a01550aULL, },
        { 0x052a00052a00052aULL, 0x00052a00052a0005ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xf3f3f3f3f3f3f3f3ULL, 0xf3f3f3f3f3f3f3f3ULL, },
        { 0xfefefefefefefefeULL, 0xfefefefefefefefeULL, },
        { 0xfcfcfcfcfcfcfcfcULL, 0xfcfcfcfcfcfcfcfcULL, },
        { 0xf9f9f9f9f9f9f9f9ULL, 0xf9f9f9f9f9f9f9f9ULL, },
        { 0xf9ffccf9ffccf9ffULL, 0xccf9ffccf9ffccf9ULL, },
        { 0xfce6fffce6fffce6ULL, 0xfffce6fffce6fffcULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x0c0c0c0c0c0c0c0cULL, 0x0c0c0c0c0c0c0c0cULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0x0303030303030303ULL, 0x0303030303030303ULL, },
        { 0x0606060606060606ULL, 0x0606060606060606ULL, },
        { 0x0600330600330600ULL, 0x3306003306003306ULL, },
        { 0x0319000319000319ULL, 0x0003190003190003ULL, },
        { 0xffff00ffff00ffffULL, 0x00ffff00ffff00ffULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xf8e30ef8e30ef8e3ULL, 0x0ef8e30ef8e30ef8ULL, },
        { 0xfffc01fffc01fffcULL, 0x01fffc01fffc01ffULL, },
        { 0xfef803fef803fef8ULL, 0x03fef803fef803feULL, },
        { 0xfcf107fcf107fcf1ULL, 0x07fcf107fcf107fcULL, },
        { 0xfcfe38fcfe38fcfeULL, 0x38fcfe38fcfe38fcULL, },
        { 0xfec700fec700fec7ULL, 0x00fec700fec700feULL, },
        { 0x0000ff0000ff0000ULL, 0xff0000ff0000ff00ULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x071cf1071cf1071cULL, 0xf1071cf1071cf107ULL, },
        { 0x0003fe0003fe0003ULL, 0xfe0003fe0003fe00ULL, },
        { 0x0107fc0107fc0107ULL, 0xfc0107fc0107fc01ULL, },
        { 0x030ef8030ef8030eULL, 0xf8030ef8030ef803ULL, },
        { 0x0301c70301c70301ULL, 0xc70301c70301c703ULL, },
        { 0x0138ff0138ff0138ULL, 0xff0138ff0138ff01ULL, },
        { 0x881afffc28180240ULL, 0x09000101ff0fb000ULL, },    /*  64  */
        { 0xf101e6f9010c0040ULL, 0x12000117ff00ec00ULL, },
        { 0xf81afff314000a40ULL, 0x00670000ff0ffd00ULL, },
        { 0x8800fffe00311501ULL, 0x02330b5eff1eec0cULL, },
        { 0xfbef00064de4fe08ULL, 0x02fff700000752ffULL, },
        { 0xfffe000c02f2ff08ULL, 0x04fff706000014ffULL, },
        { 0xffef001826fff808ULL, 0x00f7fe00020702ffULL, },
        { 0xfbff000301c9f100ULL, 0x00fbbb1a0a0f14fcULL, },
        { 0xac16fefab9f3fc80ULL, 0x04fff8fffe052501ULL, },    /*  72  */
        { 0xf501aef5fdf9ff80ULL, 0x09fff8fffd000901ULL, },
        { 0xfa16feeadcfff180ULL, 0x00d8fffff5050101ULL, },
        { 0xac00fefdfee7e2feULL, 0x01ecc6ffd50a0914ULL, },
        { 0x701300045e0cff4eULL, 0xf1fff1fffe08e2faULL, },
        { 0x0e0116090206ff4eULL, 0xe3fff1f6fd00f8faULL, },
        { 0x071300132f00fc4eULL, 0xfff1fefff508fffaULL, },
        { 0x700000020118f801ULL, 0xfcf888d8d410f8a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRA_B(b128_pattern[i], b128_pattern[j],
                         b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRA_B(b128_random[i], b128_random[j],
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
