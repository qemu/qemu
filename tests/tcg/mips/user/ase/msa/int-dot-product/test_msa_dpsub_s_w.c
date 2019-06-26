/*
 *  Test program for MSA instruction DPSUB_S.W
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
    char *instruction_name =  "DPSUB_S.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xfffffffefffffffeULL, 0xfffffffefffffffeULL, },    /*   0  */
        { 0xfffffffefffffffeULL, 0xfffffffefffffffeULL, },
        { 0xffff5552ffff5552ULL, 0xffff5552ffff5552ULL, },
        { 0xfffffffcfffffffcULL, 0xfffffffcfffffffcULL, },
        { 0xffff9994ffff9994ULL, 0xffff9994ffff9994ULL, },
        { 0xfffffffafffffffaULL, 0xfffffffafffffffaULL, },
        { 0x00001c6bffff71c0ULL, 0xffffc71500001c6bULL, },
        { 0xfffffff8fffffff8ULL, 0xfffffff8fffffff8ULL, },
        { 0xfffffff8fffffff8ULL, 0xfffffff8fffffff8ULL, },    /*   8  */
        { 0xfffffff8fffffff8ULL, 0xfffffff8fffffff8ULL, },
        { 0xfffffff8fffffff8ULL, 0xfffffff8fffffff8ULL, },
        { 0xfffffff8fffffff8ULL, 0xfffffff8fffffff8ULL, },
        { 0xfffffff8fffffff8ULL, 0xfffffff8fffffff8ULL, },
        { 0xfffffff8fffffff8ULL, 0xfffffff8fffffff8ULL, },
        { 0xfffffff8fffffff8ULL, 0xfffffff8fffffff8ULL, },
        { 0xfffffff8fffffff8ULL, 0xfffffff8fffffff8ULL, },
        { 0xffff554cffff554cULL, 0xffff554cffff554cULL, },    /*  16  */
        { 0xffff554cffff554cULL, 0xffff554cffff554cULL, },
        { 0xc71ae384c71ae384ULL, 0xc71ae384c71ae384ULL, },
        { 0xfffeaaa0fffeaaa0ULL, 0xfffeaaa0fffeaaa0ULL, },
        { 0xdddbbbb0dddbbbb0ULL, 0xdddbbbb0dddbbbb0ULL, },
        { 0xfffdfff4fffdfff4ULL, 0xfffdfff4fffdfff4ULL, },
        { 0x097912ead094f678ULL, 0xed06da06097912eaULL, },
        { 0xfffd5548fffd5548ULL, 0xfffd5548fffd5548ULL, },
        { 0xfffdfff2fffdfff2ULL, 0xfffdfff2fffdfff2ULL, },    /*  24  */
        { 0xfffdfff2fffdfff2ULL, 0xfffdfff2fffdfff2ULL, },
        { 0x38e1c70e38e1c70eULL, 0x38e1c70e38e1c70eULL, },
        { 0xfffeaa9cfffeaa9cULL, 0xfffeaa9cfffeaa9cULL, },
        { 0x2221332422213324ULL, 0x2221332422213324ULL, },
        { 0xffff5546ffff5546ULL, 0xffff5546ffff5546ULL, },
        { 0xf6845ec12f67d088ULL, 0x12f6424ff6845ec1ULL, },
        { 0xfffffff0fffffff0ULL, 0xfffffff0fffffff0ULL, },
        { 0xffff9988ffff9988ULL, 0xffff9988ffff9988ULL, },    /*  32  */
        { 0xffff9988ffff9988ULL, 0xffff9988ffff9988ULL, },
        { 0xdddcaa98dddcaa98ULL, 0xdddcaa98dddcaa98ULL, },
        { 0xffff3320ffff3320ULL, 0xffff3320ffff3320ULL, },
        { 0xeb83ae00eb83ae00ULL, 0xeb83ae00eb83ae00ULL, },
        { 0xfffeccb8fffeccb8ULL, 0xfffeccb8fffeccb8ULL, },
        { 0x05af16ace38c5af0ULL, 0xf49d9f3405af16acULL, },
        { 0xfffe6650fffe6650ULL, 0xfffe6650fffe6650ULL, },
        { 0xfffeccb6fffeccb6ULL, 0xfffeccb6fffeccb6ULL, },    /*  40  */
        { 0xfffeccb6fffeccb6ULL, 0xfffeccb6fffeccb6ULL, },
        { 0x222110fa222110faULL, 0x222110fa222110faULL, },
        { 0xffff331cffff331cULL, 0xffff331cffff331cULL, },
        { 0x147a51d4147a51d4ULL, 0x147a51d4147a51d4ULL, },
        { 0xffff9982ffff9982ULL, 0xffff9982ffff9982ULL, },
        { 0xfa4f6bff1c717d10ULL, 0x0b608e21fa4f6bffULL, },
        { 0xffffffe8ffffffe8ULL, 0xffffffe8ffffffe8ULL, },
        { 0x00001c59ffff71aeULL, 0xffffc70300001c59ULL, },    /*  48  */
        { 0x00001c59ffff71aeULL, 0xffffc70300001c59ULL, },
        { 0x097b2f4fd0966832ULL, 0xed08a115097b2f4fULL, },
        { 0x000038cafffee374ULL, 0xffff8e1e000038caULL, },
        { 0x05b082bee38c71acULL, 0xf49e609a05b082beULL, },
        { 0x0000553bfffe553aULL, 0xffff55390000553bULL, },
        { 0xf033192eca430636ULL, 0xc0c90fb0f033192eULL, },
        { 0x000071acfffdc700ULL, 0xffff1c54000071acULL, },
        { 0x00005539fffe5538ULL, 0xffff553700005539ULL, },    /*  56  */
        { 0x00005539fffe5538ULL, 0xffff553700005539ULL, },
        { 0xf68497972f66b408ULL, 0x12f5d079f6849797ULL, },
        { 0x000038c6fffee370ULL, 0xffff8e1a000038c6ULL, },
        { 0xfa4f886a1c70eed0ULL, 0x0b605536fa4f886aULL, },
        { 0x00001c53ffff71a8ULL, 0xffffc6fd00001c53ULL, },
        { 0x0fcd74d135ba3272ULL, 0x3f35d3a10fcd74d1ULL, },
        { 0xffffffe0ffffffe0ULL, 0xffffffe0ffffffe0ULL, },
        { 0xc5a8016cdd3daa5cULL, 0xe94945ebe7053037ULL, },    /*  64  */
        { 0xc3b493dce3f99616ULL, 0xe6c275fe01105522ULL, },
        { 0x949f7b2015d7bcd8ULL, 0xdd8e1f740c23f089ULL, },
        { 0xcb480f0e10df8c96ULL, 0x0470e12d02738253ULL, },
        { 0xc954a17e179b7850ULL, 0x01ea11401c7ea73eULL, },
        { 0xc9425a31f36c45a7ULL, 0xedf7684bffd4d9adULL, },
        { 0xc7fda5a7eec474caULL, 0xdbac4bfdfada4b68ULL, },
        { 0xc9d3363ecb9ded37ULL, 0xc40db8860b92e4aaULL, },
        { 0x9abe1d82fd7c13f9ULL, 0xbad961fc16a68011ULL, },    /*  72  */
        { 0x997968f8f8d4431cULL, 0xa88e45ae11abf1ccULL, },
        { 0x644cd070b0912dbbULL, 0x95a94d6df030af03ULL, },
        { 0x90151b88bce11a1cULL, 0x8ce173edd7b3566dULL, },
        { 0xc6bdaf76b7e8e9daULL, 0xb3c435a6ce02e837ULL, },
        { 0xc893400d94c26247ULL, 0x9c25a22fdebb8179ULL, },
        { 0xf45b8b25a1124ea8ULL, 0x935dc8afc63e28e3ULL, },
        { 0xc124ff9b7af87983ULL, 0x2916358ea57b0fdfULL, },
        { 0xa3bdf52f3f1bc6d3ULL, 0x1a9b7790a9e67552ULL, },    /*  80  */
        { 0xa2394ebc1f432fbaULL, 0x38d091638b040700ULL, },
        { 0x9c98e9da3d8da28dULL, 0x17578e46633c7554ULL, },
        { 0xca2304601c11139aULL, 0xecce6f4f9252c75cULL, },
        { 0xb167fd62111ca498ULL, 0xed848a6b7ffb85a6ULL, },
        { 0xb01a590af79618c4ULL, 0xcf3de0319d05b479ULL, },
        { 0xb2490b42008cb27aULL, 0xcfbf82ea8729672eULL, },
        { 0xd36607e1f75b1a82ULL, 0x8006f7ab6a0e64dcULL, },
        { 0xbf56e259efe4672cULL, 0xa61769778a2f91d2ULL, },    /*  88  */
        { 0xbe4f061a0bbba5e0ULL, 0xc922e830b7ade689ULL, },
        { 0xaac85110e5ef76abULL, 0xcc5f9db0a366adc6ULL, },
        { 0xc91b5b88fd4a93d2ULL, 0x879c58c17a96cfbaULL, },
        { 0xb8799dfa21be5efeULL, 0xa721331f6c3d78f0ULL, },
        { 0xb76ef97e2ca86ef4ULL, 0xbb78ca223c0de8adULL, },
        { 0x9da743266b64f51cULL, 0xba24b1045354f4faULL, },
        { 0xc2f3162f429e4870ULL, 0x764125c06e4d3512ULL, },
        { 0xa89d5e1d1ffccbf4ULL, 0x51bf6a197f87f33bULL, },    /*  96  */
        { 0x890f17ff2c462c7cULL, 0x34f589127c4cc49aULL, },
        { 0x53dc26951679feb0ULL, 0x2aa458e36a7c8cdeULL, },
        { 0x7ed4f0c1135e605eULL, 0x1a22c08d472920e2ULL, },
        { 0x80f6d8c622f1e674ULL, 0x071f986d36987e53ULL, },
        { 0x7ee91ba012abf971ULL, 0xeab87172091da737ULL, },
        { 0x80fac8d20b8e2fb8ULL, 0x0ad43e562523cff0ULL, },
        { 0x7ef3481012ac516eULL, 0x1acdbd0e31a33d13ULL, },
        { 0xbf53a8023cd97b5aULL, 0x07b9c024393d8136ULL, },    /* 104  */
        { 0x8e3cb38085aaebe3ULL, 0xf84dd1305e923ebfULL, },
        { 0x50c22f685af8caedULL, 0xef14166874d2544dULL, },
        { 0x7a3548245bc2dee5ULL, 0xf6b38ff08f52b803ULL, },
        { 0x3e4f96f53628fefdULL, 0xbe65c7ed60e1faffULL, },
        { 0x2c2056e3221de63fULL, 0x871151e081227a9dULL, },
        { 0x113314bc1293f380ULL, 0x774bb8df643781b9ULL, },
        { 0x07d911730a4b3a5dULL, 0x8b56a81c77aef6ebULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPSUB_S_W(b128_pattern[i], b128_pattern[j],
                             b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPSUB_S_W(b128_random[i], b128_random[j],
                             b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                          (PATTERN_INPUTS_SHORT_COUNT)) +
                                         RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPSUB_S_W__DDT(b128_random[i], b128_random[j],
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
            do_msa_DPSUB_S_W__DSD(b128_random[i], b128_random[j],
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
