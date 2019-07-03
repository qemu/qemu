/*
 *  Test program for MSA instruction MSUBR_Q.H
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
    char *instruction_name =  "MSUBR_Q.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x00000000ffff0000ULL, 0x0000ffff00000000ULL, },
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
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xc71bc71bc71bc71bULL, 0xc71bc71bc71bc71bULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xdddcdddcdddcdddcULL, 0xdddcdddcdddcdddcULL, },
        { 0xfffefffefffefffeULL, 0xfffefffefffefffeULL, },
        { 0xed0725ebb423ed07ULL, 0x25ebb423ed0725ebULL, },
        { 0xfffdfffdfffefffdULL, 0xfffdfffefffdfffdULL, },
        { 0xfffefffefffffffeULL, 0xfffefffffffefffeULL, },    /*  24  */
        { 0xfffefffefffffffeULL, 0xfffefffffffefffeULL, },
        { 0x38e238e238e338e2ULL, 0x38e238e338e238e2ULL, },
        { 0xffffffff0000ffffULL, 0xffff0000ffffffffULL, },
        { 0x2222222222232222ULL, 0x2222222322222222ULL, },
        { 0x0000000000010000ULL, 0x0000000100000000ULL, },
        { 0x12f7da134bdb12f7ULL, 0xda134bdb12f7da13ULL, },
        { 0x0001000000010001ULL, 0x0000000100010000ULL, },
        { 0x0001000000010001ULL, 0x0000000100010000ULL, },    /*  32  */
        { 0x0001000000010001ULL, 0x0000000100010000ULL, },
        { 0xdddedddddddedddeULL, 0xdddddddedddeddddULL, },
        { 0x0001000000010001ULL, 0x0000000100010000ULL, },
        { 0xeb85eb84eb85eb85ULL, 0xeb84eb85eb85eb84ULL, },
        { 0x0000ffff00000000ULL, 0xffff00000000ffffULL, },
        { 0xf49f16c1d27cf49fULL, 0x16c1d27cf49f16c1ULL, },
        { 0x0000ffff00000000ULL, 0xffff00000000ffffULL, },
        { 0x0000ffff00000000ULL, 0xffff00000000ffffULL, },    /*  40  */
        { 0x0000ffff00000000ULL, 0xffff00000000ffffULL, },
        { 0x2222222122222222ULL, 0x2221222222222221ULL, },
        { 0x0000ffff00000000ULL, 0xffff00000000ffffULL, },
        { 0x147b147a147b147bULL, 0x147a147b147b147aULL, },
        { 0x0000ffff00000000ULL, 0xffff00000000ffffULL, },
        { 0x0b61e93e2d830b61ULL, 0xe93e2d830b61e93eULL, },
        { 0x0001000000000001ULL, 0x0000000000010000ULL, },
        { 0x00010000ffff0001ULL, 0x0000ffff00010000ULL, },    /*  48  */
        { 0x00010000ffff0001ULL, 0x0000ffff00010000ULL, },
        { 0xed0a25edb424ed0aULL, 0x25edb424ed0a25edULL, },
        { 0x00010000fffe0001ULL, 0x0000fffe00010000ULL, },
        { 0xf4a016c2d27af4a0ULL, 0x16c2d27af4a016c2ULL, },
        { 0x00010001fffd0001ULL, 0x0001fffd00010001ULL, },
        { 0xf9afe6b99ad9f9afULL, 0xe6b99ad9f9afe6b9ULL, },
        { 0x00010002fffc0001ULL, 0x0002fffc00010002ULL, },
        { 0x00010002fffd0001ULL, 0x0002fffd00010002ULL, },    /*  56  */
        { 0x00010002fffd0001ULL, 0x0002fffd00010002ULL, },
        { 0x12f7da144bd812f7ULL, 0xda144bd812f7da14ULL, },
        { 0x00010001fffe0001ULL, 0x0001fffe00010001ULL, },
        { 0x0b62e93f2d820b62ULL, 0xe93f2d820b62e93fULL, },
        { 0x00020001ffff0002ULL, 0x0001ffff00020001ULL, },
        { 0x0654194a65220654ULL, 0x194a65220654194aULL, },
        { 0x00020001ffff0002ULL, 0x0001ffff00020001ULL, },
        { 0x9048fb0bf341c73bULL, 0xd396fefdfffdce10ULL, },    /*  64  */
        { 0x8c4efb1edac8ed2cULL, 0xc86a051b003e01e6ULL, },
        { 0x8000eb1af0ed3ac3ULL, 0xb0f10a2bff3c190fULL, },
        { 0xe8edef7ed3364e8aULL, 0xf42214c0fe3406b6ULL, },
        { 0xe4f3ef91babd747bULL, 0xe8f61adefe753a8cULL, },
        { 0xe4cfef918bb95b20ULL, 0xe627f5c8faee04bfULL, },
        { 0xe207efd0b6432746ULL, 0xe040d7190903ecb5ULL, },
        { 0xe5c3efbf80001a0fULL, 0xf12696f61769ffc0ULL, },
        { 0x979ddfbb962567a6ULL, 0xd9ad9c06166716e9ULL, },    /*  72  */
        { 0x94d5dffac0af33ccULL, 0xd3c68000247cfedfULL, },
        { 0x8000ac4b9a31c9c4ULL, 0xc75f8000ec43f421ULL, },
        { 0xc965ba77cdd8aebdULL, 0xeae08000b2c6fca3ULL, },
        { 0x3252bedbb021c284ULL, 0x2e118a95b1beea4aULL, },
        { 0x360ebeca8000b54dULL, 0x3ef78000c024fd55ULL, },
        { 0x7f73ccf6b3a79a46ULL, 0x6278800086a705d7ULL, },
        { 0x1ce9c91380009362ULL, 0xfcd580008000ff19ULL, },
        { 0x37ebbe42a862dbb9ULL, 0xfeb38b5e8000fe89ULL, },    /*  80  */
        { 0x39c7be75dd7ccb94ULL, 0xfee48000953fff7cULL, },
        { 0x5f8994cfca8f9bdeULL, 0xff3c80008000ffa2ULL, },
        { 0x0bb6a77cf1e284a3ULL, 0xfe8d80008000ff8cULL, },
        { 0x16a7960ef656d6ccULL, 0xff688b5e8000ff44ULL, },
        { 0x17689660fc31c475ULL, 0xff7f8000953fffbeULL, },
        { 0x26b48000fa1a8e43ULL, 0xffa780008000ffd1ULL, },
        { 0x04bf964dfe718000ULL, 0xff5880008000ffc6ULL, },
        { 0x092e817dfeefd540ULL, 0xffbb8b5e8000ffa2ULL, },    /*  88  */
        { 0x097c81dfff94c239ULL, 0xffc58000953fffdfULL, },
        { 0x0faf8000ff5989ffULL, 0xffd780008000ffe9ULL, },
        { 0x01ec964dffd48000ULL, 0xffb280008000ffe4ULL, },
        { 0x03b8817dffe2d540ULL, 0xffe08b5e8000ffd3ULL, },
        { 0x03d881dffff4c239ULL, 0xffe58000953ffff0ULL, },
        { 0x065b8000ffed89ffULL, 0xffed80008000fff5ULL, },
        { 0x00c7964dfffb8000ULL, 0xffdc80008000fff2ULL, },
        { 0x0181817dfffdd540ULL, 0xfff18b5e8000ffe9ULL, },    /*  96  */
        { 0x02e98000fffef1b9ULL, 0xfffa95ba8000ffdbULL, },
        { 0x05a18000fffffb3bULL, 0xfffe9f2a8000ffc4ULL, },
        { 0x0ae38000fffffe68ULL, 0xffffa7c48000ff9fULL, },
        { 0x0b4080630000fdb2ULL, 0xffff8000953fffdeULL, },
        { 0x0ba080c60000fcabULL, 0xffff8000a6f7fff4ULL, },
        { 0x0c0381280000fb2fULL, 0xffff8000b5befffcULL, },
        { 0x0c69818a0000f90aULL, 0xffff8000c211ffffULL, },
        { 0x148580000000f2b4ULL, 0xffff80009905ffffULL, },    /* 104  */
        { 0x21ee80000000e69aULL, 0xffff80008000ffffULL, },
        { 0x381a80000000cf7cULL, 0xffff80008000ffffULL, },
        { 0x5cc480000000a354ULL, 0xffff80008000ffffULL, },
        { 0x0b5f964d00008dd4ULL, 0xfffe80008000ffffULL, },
        { 0x0165a8b700008000ULL, 0xfffc80008000ffffULL, },
        { 0x002cb7ec00008000ULL, 0xfff880008000ffffULL, },
        { 0x0005c47b00008000ULL, 0xfff180008000ffffULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MSUBR_Q_H(b128_pattern[i], b128_pattern[j],
                             b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MSUBR_Q_H(b128_random[i], b128_random[j],
                             b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                          (PATTERN_INPUTS_SHORT_COUNT)) +
                                         RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MSUBR_Q_H__DDT(b128_random[i], b128_random[j],
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
            do_msa_MSUBR_Q_H__DSD(b128_random[i], b128_random[j],
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
