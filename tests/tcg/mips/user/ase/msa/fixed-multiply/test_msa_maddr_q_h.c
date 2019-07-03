/*
 *  Test program for MSA instruction MADDR_Q.H
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
            3 * (RANDOM_INPUTS_SHORT_COUNT) * (RANDOM_INPUTS_SHORT_COUNT))


int32_t main(void)
{
    char *isa_ase_name = "MSA";
    char *group_name = "Fixed Multiply";
    char *instruction_name =  "MADDR_Q.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000010000ULL, 0x0000000100000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },    /*  16  */
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x38e538e538e538e5ULL, 0x38e538e538e538e5ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x2224222422242224ULL, 0x2224222422242224ULL, },
        { 0x0002000200020002ULL, 0x0002000200020002ULL, },
        { 0x12f9da154bdd12f9ULL, 0xda154bdd12f9da15ULL, },
        { 0x0003000300020003ULL, 0x0003000200030003ULL, },
        { 0x0002000200010002ULL, 0x0002000100020002ULL, },    /*  24  */
        { 0x0002000200010002ULL, 0x0002000100020002ULL, },
        { 0xc71ec71ec71dc71eULL, 0xc71ec71dc71ec71eULL, },
        { 0x0001000100000001ULL, 0x0001000000010001ULL, },
        { 0xdddedddedddddddeULL, 0xdddedddddddedddeULL, },
        { 0x00000000ffff0000ULL, 0x0000ffff00000000ULL, },
        { 0xed0925edb425ed09ULL, 0x25edb425ed0925edULL, },
        { 0xffff0000ffffffffULL, 0x0000ffffffff0000ULL, },
        { 0xffff0000ffffffffULL, 0x0000ffffffff0000ULL, },    /*  32  */
        { 0xffff0000ffffffffULL, 0x0000ffffffff0000ULL, },
        { 0x2222222322222222ULL, 0x2223222222222223ULL, },
        { 0xffff0000ffffffffULL, 0x0000ffffffff0000ULL, },
        { 0x147b147c147b147bULL, 0x147c147b147b147cULL, },
        { 0x0000000100000000ULL, 0x0001000000000001ULL, },
        { 0x0b61e93f2d840b61ULL, 0xe93f2d840b61e93fULL, },
        { 0x0000000100000000ULL, 0x0001000000000001ULL, },
        { 0x0000000100000000ULL, 0x0001000000000001ULL, },    /*  40  */
        { 0x0000000100000000ULL, 0x0001000000000001ULL, },
        { 0xdddedddfdddedddeULL, 0xdddfdddedddedddfULL, },
        { 0x0000000100000000ULL, 0x0001000000000001ULL, },
        { 0xeb85eb86eb85eb85ULL, 0xeb86eb85eb85eb86ULL, },
        { 0x0000000100000000ULL, 0x0001000000000001ULL, },
        { 0xf49f16c2d27df49fULL, 0x16c2d27df49f16c2ULL, },
        { 0xffff00000000ffffULL, 0x00000000ffff0000ULL, },
        { 0xffff00000001ffffULL, 0x00000001ffff0000ULL, },    /*  48  */
        { 0xffff00000001ffffULL, 0x00000001ffff0000ULL, },
        { 0x12f6da134bdc12f6ULL, 0xda134bdc12f6da13ULL, },
        { 0xffff00000002ffffULL, 0x00000002ffff0000ULL, },
        { 0x0b60e93e2d860b60ULL, 0xe93e2d860b60e93eULL, },
        { 0xffffffff0003ffffULL, 0xffff0003ffffffffULL, },
        { 0x0651194765270651ULL, 0x1947652706511947ULL, },
        { 0xfffffffe0004ffffULL, 0xfffe0004fffffffeULL, },
        { 0xfffffffe0003ffffULL, 0xfffe0003fffffffeULL, },    /*  56  */
        { 0xfffffffe0003ffffULL, 0xfffe0003fffffffeULL, },
        { 0xed0925ecb428ed09ULL, 0x25ecb428ed0925ecULL, },
        { 0xffffffff0002ffffULL, 0xffff0002ffffffffULL, },
        { 0xf49e16c1d27ef49eULL, 0x16c1d27ef49e16c1ULL, },
        { 0xfffeffff0001fffeULL, 0xffff0001fffeffffULL, },
        { 0xf9ace6b69adef9acULL, 0xe6b69adef9ace6b6ULL, },
        { 0xfffeffff0001fffeULL, 0xffff0001fffeffffULL, },
        { 0x6fb804f50cbf38c5ULL, 0x2c6a0103000331f0ULL, },    /*  64  */
        { 0x73b204e2253812d4ULL, 0x3796fae5ffc2fe1aULL, },
        { 0x7fff14e60f13c53dULL, 0x4f0ff5d500c4e6f1ULL, },
        { 0x171210822ccab176ULL, 0x0bdeeb4001ccf94aULL, },
        { 0x1b0c106f45438b85ULL, 0x170ae522018bc574ULL, },
        { 0x1b30106f7447a4e0ULL, 0x19d90a380512fb41ULL, },
        { 0x1df8103049bdd8baULL, 0x1fc028e7f6fd134bULL, },
        { 0x1a3c10417fffe5f1ULL, 0x0eda690ae8970040ULL, },
        { 0x6862204569da985aULL, 0x265363fae999e917ULL, },    /*  72  */
        { 0x6b2a20063f50cc34ULL, 0x2c3a7fffdb840121ULL, },
        { 0x7fff53b565ce363dULL, 0x38a17fff13bd0bdfULL, },
        { 0x369a458932275144ULL, 0x15207fff4d3a035dULL, },
        { 0xcdad41254fde3d7dULL, 0xd1ef756a4e4215b6ULL, },
        { 0xc9f141367fff4ab4ULL, 0xc1097fff3fdc02abULL, },
        { 0x808c330a4c5865bbULL, 0x9d887fff7959fa29ULL, },
        { 0xe31636ed7fff6c9fULL, 0x032b7fff7fff00e7ULL, },
        { 0xfe192c1c7fff7fffULL, 0x05097fff7e7a0057ULL, },    /*  80  */
        { 0xfe292c3e7fff4707ULL, 0x05c83b1a7fff008fULL, },
        { 0xff5d102139cf0662ULL, 0x079520c82b2b00b8ULL, },
        { 0xfece12f0645904e7ULL, 0x00d302440dea008eULL, },
        { 0xffec0f357fff082bULL, 0x014f02780dc00035ULL, },
        { 0xffed0f417fff0488ULL, 0x0181012410080057ULL, },
        { 0xfff9059039cf0068ULL, 0x01f900a205680070ULL, },
        { 0xfff3068864590050ULL, 0x0037000b01be0056ULL, },
        { 0xffff053f7fff0085ULL, 0x0057000c01b90020ULL, },    /*  88  */
        { 0xffff05437fff004aULL, 0x0064000602020035ULL, },
        { 0x000001eb39cf0007ULL, 0x0083000300ad0044ULL, },
        { 0x0000024164590005ULL, 0x000e000000380034ULL, },
        { 0x000001cf7fff0008ULL, 0x0016000000370014ULL, },
        { 0x000001d07fff0004ULL, 0x0019000000400021ULL, },
        { 0x000000a939cf0000ULL, 0x002100000016002bULL, },
        { 0x000000c664590000ULL, 0x0004000000070021ULL, },
        { 0x0000009f7fff0000ULL, 0x000600000007000cULL, },    /*  96  */
        { 0x000000807fff0000ULL, 0x000a000000070005ULL, },
        { 0x000000677fff0000ULL, 0x0010000000070002ULL, },
        { 0x000000537fff0000ULL, 0x0019000000070001ULL, },
        { 0x000000537fff0000ULL, 0x001d000000080002ULL, },
        { 0x000000537fff0000ULL, 0x0021000000090003ULL, },
        { 0x000000537fff0000ULL, 0x00260000000a0005ULL, },
        { 0x000000537fff0000ULL, 0x002c0000000c0008ULL, },
        { 0x0000001e39cf0000ULL, 0x003a00000004000aULL, },    /* 104  */
        { 0x0000000b1a1c0000ULL, 0x004c00000001000dULL, },
        { 0x000000040bcb0000ULL, 0x0064000000000011ULL, },
        { 0x0000000105530000ULL, 0x0083000000000016ULL, },
        { 0x00000001093e0000ULL, 0x000e000000000011ULL, },
        { 0x00000001100b0000ULL, 0x000200000000000dULL, },
        { 0x000000011bd90000ULL, 0x000000000000000aULL, },
        { 0x0000000130570000ULL, 0x0000000000000008ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADDR_Q_H(b128_pattern[i], b128_pattern[j],
                             b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADDR_Q_H(b128_random[i], b128_random[j],
                             b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                          (PATTERN_INPUTS_SHORT_COUNT)) +
                                         RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADDR_Q_H__DDT(b128_random[i], b128_random[j],
                                  b128_result[
                                      ((PATTERN_INPUTS_SHORT_COUNT) *
                                       (PATTERN_INPUTS_SHORT_COUNT)) +
                                      ((RANDOM_INPUTS_SHORT_COUNT) *
                                       (RANDOM_INPUTS_SHORT_COUNT)) +
                                      RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADDR_Q_H__DSD(b128_random[i], b128_random[j],
                                  b128_result[
                                      ((PATTERN_INPUTS_SHORT_COUNT) *
                                       (PATTERN_INPUTS_SHORT_COUNT)) +
                                      (2 * (RANDOM_INPUTS_SHORT_COUNT) *
                                       (RANDOM_INPUTS_SHORT_COUNT)) +
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
