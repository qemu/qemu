/*
 *  Test program for MSA instruction DPADD_S.H
 *
 *  Copyright (C) 2019  Wave Computing, Inc.
 *  Copyright (C) 2019  Aleksandar Markovic <amarkovic@wavecomp.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *`
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
    char *group_name = "Int Dot Product";
    char *instruction_name =  "DPADD_S.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0002000200020002ULL, 0x0002000200020002ULL, },    /*   0  */
        { 0x0002000200020002ULL, 0x0002000200020002ULL, },
        { 0x00ae00ae00ae00aeULL, 0x00ae00ae00ae00aeULL, },
        { 0x0004000400040004ULL, 0x0004000400040004ULL, },
        { 0x006c006c006c006cULL, 0x006c006c006c006cULL, },
        { 0x0006000600060006ULL, 0x0006000600060006ULL, },
        { 0x0095ffeb00400095ULL, 0xffeb00400095ffebULL, },
        { 0x0008000800080008ULL, 0x0008000800080008ULL, },
        { 0x0008000800080008ULL, 0x0008000800080008ULL, },    /*   8  */
        { 0x0008000800080008ULL, 0x0008000800080008ULL, },
        { 0x0008000800080008ULL, 0x0008000800080008ULL, },
        { 0x0008000800080008ULL, 0x0008000800080008ULL, },
        { 0x0008000800080008ULL, 0x0008000800080008ULL, },
        { 0x0008000800080008ULL, 0x0008000800080008ULL, },
        { 0x0008000800080008ULL, 0x0008000800080008ULL, },
        { 0x0008000800080008ULL, 0x0008000800080008ULL, },
        { 0x00b400b400b400b4ULL, 0x00b400b400b400b4ULL, },    /*  16  */
        { 0x00b400b400b400b4ULL, 0x00b400b400b400b4ULL, },
        { 0x3a7c3a7c3a7c3a7cULL, 0x3a7c3a7c3a7c3a7cULL, },
        { 0x0160016001600160ULL, 0x0160016001600160ULL, },
        { 0x2450245024502450ULL, 0x2450245024502450ULL, },
        { 0x020c020c020c020cULL, 0x020c020c020c020cULL, },
        { 0x3216f8fa15883216ULL, 0xf8fa15883216f8faULL, },
        { 0x02b802b802b802b8ULL, 0x02b802b802b802b8ULL, },
        { 0x020e020e020e020eULL, 0x020e020e020e020eULL, },    /*  24  */
        { 0x020e020e020e020eULL, 0x020e020e020e020eULL, },
        { 0xc8f2c8f2c8f2c8f2ULL, 0xc8f2c8f2c8f2c8f2ULL, },
        { 0x0164016401640164ULL, 0x0164016401640164ULL, },
        { 0xdedcdedcdedcdedcULL, 0xdedcdedcdedcdedcULL, },
        { 0x00ba00ba00ba00baULL, 0x00ba00ba00ba00baULL, },
        { 0xd13f09b1ed78d13fULL, 0x09b1ed78d13f09b1ULL, },
        { 0x0010001000100010ULL, 0x0010001000100010ULL, },
        { 0x0078007800780078ULL, 0x0078007800780078ULL, },    /*  32  */
        { 0x0078007800780078ULL, 0x0078007800780078ULL, },
        { 0x2368236823682368ULL, 0x2368236823682368ULL, },
        { 0x00e000e000e000e0ULL, 0x00e000e000e000e0ULL, },
        { 0x1600160016001600ULL, 0x1600160016001600ULL, },
        { 0x0148014801480148ULL, 0x0148014801480148ULL, },
        { 0x1e54fbcc0d101e54ULL, 0xfbcc0d101e54fbccULL, },
        { 0x01b001b001b001b0ULL, 0x01b001b001b001b0ULL, },
        { 0x014a014a014a014aULL, 0x014a014a014a014aULL, },    /*  40  */
        { 0x014a014a014a014aULL, 0x014a014a014a014aULL, },
        { 0xdf06df06df06df06ULL, 0xdf06df06df06df06ULL, },
        { 0x00e400e400e400e4ULL, 0x00e400e400e400e4ULL, },
        { 0xec2cec2cec2cec2cULL, 0xec2cec2cec2cec2cULL, },
        { 0x007e007e007e007eULL, 0x007e007e007e007eULL, },
        { 0xe40105dff4f0e401ULL, 0x05dff4f0e40105dfULL, },
        { 0x0018001800180018ULL, 0x0018001800180018ULL, },
        { 0x00a7fffd005200a7ULL, 0xfffd005200a7fffdULL, },    /*  48  */
        { 0x00a7fffd005200a7ULL, 0xfffd005200a7fffdULL, },
        { 0x30b1f6eb13ce30b1ULL, 0xf6eb13ce30b1f6ebULL, },
        { 0x0136ffe2008c0136ULL, 0xffe2008c0136ffe2ULL, },
        { 0x1e42fa660c541e42ULL, 0xfa660c541e42fa66ULL, },
        { 0x01c5ffc700c601c5ULL, 0xffc700c601c5ffc7ULL, },
        { 0x37d20f503fca37d2ULL, 0x0f503fca37d20f50ULL, },
        { 0x0254ffac01000254ULL, 0xffac01000254ffacULL, },
        { 0x01c7ffc900c801c7ULL, 0xffc900c801c7ffc9ULL, },    /*  56  */
        { 0x01c7ffc900c801c7ULL, 0xffc900c801c7ffc9ULL, },
        { 0xd2690987edf8d269ULL, 0x0987edf8d2690987ULL, },
        { 0x013affe60090013aULL, 0xffe60090013affe6ULL, },
        { 0xe49605caf530e496ULL, 0x05caf530e49605caULL, },
        { 0x00ad0003005800adULL, 0x0003005800ad0003ULL, },
        { 0xcb2ff05fc18ecb2fULL, 0xf05fc18ecb2ff05fULL, },
        { 0x0020002000200020ULL, 0x0020002000200020ULL, },
        { 0x64440d542be42c59ULL, 0x3f8a231d3b3d19b0ULL, },    /*  64  */
        { 0x4b48f9380e321b6cULL, 0x413129b25958ffe0ULL, },
        { 0x97ec1304f058d493ULL, 0x3c8626d66eabf540ULL, },
        { 0x8422012411cade1dULL, 0x14cc12fe8f0ffa20ULL, },
        { 0x6b26ed08f418cd30ULL, 0x16731993ad2ae050ULL, },
        { 0x7c43135139aada21ULL, 0x18082ed0be64faa4ULL, },
        { 0x66b3f20f392cf02eULL, 0x1c2e3e58c200062eULL, },
        { 0x50250fd64095f94cULL, 0x149f5aa0cb1bfe12ULL, },
        { 0x9cc929a222bbb273ULL, 0x0ff457c4e06ef372ULL, },    /*  72  */
        { 0x87390860223dc880ULL, 0x141a674ce40afefcULL, },
        { 0xc26d3f883f4f3df9ULL, 0x204b7471077c05e5ULL, },
        { 0xb9731e9e1bdc24afULL, 0x111e8fc92f75fa0fULL, },
        { 0xa5a90cbe3d4e2e39ULL, 0xe9647bf14fd9feefULL, },
        { 0x8f1b2a8544b73757ULL, 0xe1d5983958f4f6d3ULL, },
        { 0x8621099b21441e0dULL, 0xd2a8b39180edeafdULL, },
        { 0xcf8222a84d293955ULL, 0x0732f211af821281ULL, },
        { 0xb24e311468e36182ULL, 0x1d5df7b5739a06edULL, },    /*  80  */
        { 0x9fb838d0948447f9ULL, 0x1c22f28463ef0925ULL, },
        { 0xa63c3700ca342b06ULL, 0x1b16f62c40350d56ULL, },
        { 0x91603bbac05427d0ULL, 0x0dabf3fc381feb90ULL, },
        { 0xed2843f4d67c28c3ULL, 0xef47f1f54694ece0ULL, },
        { 0xe3373f50950e1df3ULL, 0xeb96f4e231bee6f8ULL, },
        { 0x00111042b00d1732ULL, 0xf8f3f7b81663e296ULL, },
        { 0x0550257c952a23bcULL, 0xfd4e0730286f0ddaULL, },
        { 0x2418088a94861e5bULL, 0x1bcf191d5d740802ULL, },    /*  88  */
        { 0x1d34dae8a7fc1a85ULL, 0x1f6e155281a10a8aULL, },
        { 0x25f8ef24c16f4c23ULL, 0x12f7103e9bd702c4ULL, },
        { 0x33b0f882bf8c4de5ULL, 0x0b68ff0eb3981908ULL, },
        { 0xfaa812ea88fc60b6ULL, 0x38790427823a1198ULL, },
        { 0x11760a6866984906ULL, 0x38280709862a18aaULL, },
        { 0x355ee4445e3624a9ULL, 0x3a70056ab5ba156aULL, },
        { 0x6990f6508b1005efULL, 0x19d2f282bd2beb34ULL, },
        { 0x09f8e7147ee80358ULL, 0x0ea3c3a4d25af434ULL, },    /*  96  */
        { 0x0270e58e89681a57ULL, 0xed529f3dfdf4fa64ULL, },
        { 0x2fe0ff749ea038b9ULL, 0x08bfb178f83600f4ULL, },
        { 0x0c98e7fe6a903991ULL, 0xf0f0da2312380064ULL, },
        { 0x272ce738ba222968ULL, 0xf060e7ef217afed4ULL, },
        { 0x1b11fce0969a2387ULL, 0xebe0ecf24235fee0ULL, },
        { 0x1628f080a22617f4ULL, 0xeb86f0ea54aafebcULL, },
        { 0x0b6abf0075b21275ULL, 0xee56f2fe4664ff28ULL, },
        { 0x2d12d3d2642dcfbbULL, 0xde28f62c3ff20223ULL, },    /* 104  */
        { 0x24a2f1b03fd408a0ULL, 0xd2baf84428ad0529ULL, },
        { 0xf7c6115e36c734f8ULL, 0xd6a8f9d00d740916ULL, },
        { 0xe656ec5832b62134ULL, 0xde02fb961c9f0c1bULL, },
        { 0xf580051836e82d2eULL, 0xed2a0e7efa190093ULL, },
        { 0xc9300cbe462435ecULL, 0xf33df43e02952973ULL, },
        { 0xbff0f9ec66bc299eULL, 0xf581f02ee651f985ULL, },
        { 0x9e90f34e7f2c06f4ULL, 0x01e3f07e04092877ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPADD_S_H(b128_pattern[i], b128_pattern[j],
                             b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPADD_S_H(b128_random[i], b128_random[j],
                             b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                          (PATTERN_INPUTS_SHORT_COUNT)) +
                                         RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPADD_S_H__DDT(b128_random[i], b128_random[j],
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
            do_msa_DPADD_S_H__DSD(b128_random[i], b128_random[j],
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
