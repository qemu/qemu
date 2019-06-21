/*
 *  Test program for MSA instruction SRAR.B
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
    char *instruction_name =  "SRAR.B";
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
        { 0x0000ff0000ff0000ULL, 0xff0000ff0000ff00ULL, },
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
        { 0xebebebebebebebebULL, 0xebebebebebebebebULL, },
        { 0xfdfdfdfdfdfdfdfdULL, 0xfdfdfdfdfdfdfdfdULL, },
        { 0xfbfbfbfbfbfbfbfbULL, 0xfbfbfbfbfbfbfbfbULL, },
        { 0xf5f5f5f5f5f5f5f5ULL, 0xf5f5f5f5f5f5f5f5ULL, },
        { 0xf5ffaaf5ffaaf5ffULL, 0xaaf5ffaaf5ffaaf5ULL, },
        { 0xfbd5fffbd5fffbd5ULL, 0xfffbd5fffbd5fffbULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x1515151515151515ULL, 0x1515151515151515ULL, },
        { 0x0303030303030303ULL, 0x0303030303030303ULL, },
        { 0x0505050505050505ULL, 0x0505050505050505ULL, },
        { 0x0b0b0b0b0b0b0b0bULL, 0x0b0b0b0b0b0b0b0bULL, },
        { 0x0b01550b01550b01ULL, 0x550b01550b01550bULL, },
        { 0x052b01052b01052bULL, 0x01052b01052b0105ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xf3f3f3f3f3f3f3f3ULL, 0xf3f3f3f3f3f3f3f3ULL, },
        { 0xfefefefefefefefeULL, 0xfefefefefefefefeULL, },
        { 0xfdfdfdfdfdfdfdfdULL, 0xfdfdfdfdfdfdfdfdULL, },
        { 0xfafafafafafafafaULL, 0xfafafafafafafafaULL, },
        { 0xfaffccfaffccfaffULL, 0xccfaffccfaffccfaULL, },
        { 0xfde600fde600fde6ULL, 0x00fde600fde600fdULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x0d0d0d0d0d0d0d0dULL, 0x0d0d0d0d0d0d0d0dULL, },
        { 0x0202020202020202ULL, 0x0202020202020202ULL, },
        { 0x0303030303030303ULL, 0x0303030303030303ULL, },
        { 0x0606060606060606ULL, 0x0606060606060606ULL, },
        { 0x0601330601330601ULL, 0x3306013306013306ULL, },
        { 0x031a00031a00031aULL, 0x00031a00031a0003ULL, },
        { 0x00ff0000ff0000ffULL, 0x0000ff0000ff0000ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xf9e40ef9e40ef9e4ULL, 0x0ef9e40ef9e40ef9ULL, },
        { 0xfffc02fffc02fffcULL, 0x02fffc02fffc02ffULL, },
        { 0xfef904fef904fef9ULL, 0x04fef904fef904feULL, },
        { 0xfcf207fcf207fcf2ULL, 0x07fcf207fcf207fcULL, },
        { 0xfcfe38fcfe38fcfeULL, 0x38fcfe38fcfe38fcULL, },
        { 0xfec700fec700fec7ULL, 0x00fec700fec700feULL, },
        { 0x0001000001000001ULL, 0x0000010000010000ULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x071cf2071cf2071cULL, 0xf2071cf2071cf207ULL, },
        { 0x0104fe0104fe0104ULL, 0xfe0104fe0104fe01ULL, },
        { 0x0207fc0207fc0207ULL, 0xfc0207fc0207fc02ULL, },
        { 0x040ef9040ef9040eULL, 0xf9040ef9040ef904ULL, },
        { 0x0402c70402c70402ULL, 0xc70402c70402c704ULL, },
        { 0x0239000239000239ULL, 0x0002390002390002ULL, },
        { 0x881b00fd28190340ULL, 0x09010101000fb001ULL, },    /*  64  */
        { 0xf102e6fa010c0140ULL, 0x130101180001ec01ULL, },
        { 0xf91b00f314010b40ULL, 0x01670001000ffe01ULL, },
        { 0x880100fe01311501ULL, 0x02340b5eff1fec0cULL, },
        { 0xfbf000064de5fe08ULL, 0x0200f70000085200ULL, },
        { 0xffff000c02f20008ULL, 0x0500f70701001500ULL, },
        { 0x00f0001927fff908ULL, 0x00f7ff0003080300ULL, },
        { 0xfbff000301caf200ULL, 0x01fcbb1a0b1015fcULL, },
        { 0xac17fffbb9f4fc80ULL, 0x0500f900ff052501ULL, },    /*  72  */
        { 0xf601aef5fefaff80ULL, 0x0a00f900fd000901ULL, },
        { 0xfb17ffebdd00f180ULL, 0x00d8ff00f5050101ULL, },
        { 0xac01fffdffe8e3feULL, 0x01ecc6ffd60b0914ULL, },
        { 0x701400055e0cff4eULL, 0xf200f1ffff08e2faULL, },
        { 0x0e01160a0306004eULL, 0xe300f1f6fd01f9faULL, },
        { 0x071400132f00fc4eULL, 0xfff1fe00f508fffaULL, },
        { 0x700100020119f901ULL, 0xfcf988d8d511f9a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRAR_B(b128_pattern[i], b128_pattern[j],
                          b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRAR_B(b128_random[i], b128_random[j],
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
