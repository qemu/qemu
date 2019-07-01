/*
 *  Test program for MSA instruction DPSUB_U.W
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
    char *instruction_name =  "DPSUB_U.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0003fffe0003fffeULL, 0x0003fffe0003fffeULL, },    /*   0  */
        { 0x0003fffe0003fffeULL, 0x0003fffe0003fffeULL, },
        { 0xaab15552aab15552ULL, 0xaab15552aab15552ULL, },
        { 0x0007fffc0007fffcULL, 0x0007fffc0007fffcULL, },
        { 0x6671999466719994ULL, 0x6671999466719994ULL, },
        { 0x000bfffa000bfffaULL, 0x000bfffa000bfffaULL, },
        { 0xe39c1c6b8e4771c0ULL, 0x38f1c715e39c1c6bULL, },
        { 0x000ffff8000ffff8ULL, 0x000ffff8000ffff8ULL, },
        { 0x000ffff8000ffff8ULL, 0x000ffff8000ffff8ULL, },    /*   8  */
        { 0x000ffff8000ffff8ULL, 0x000ffff8000ffff8ULL, },
        { 0x000ffff8000ffff8ULL, 0x000ffff8000ffff8ULL, },
        { 0x000ffff8000ffff8ULL, 0x000ffff8000ffff8ULL, },
        { 0x000ffff8000ffff8ULL, 0x000ffff8000ffff8ULL, },
        { 0x000ffff8000ffff8ULL, 0x000ffff8000ffff8ULL, },
        { 0x000ffff8000ffff8ULL, 0x000ffff8000ffff8ULL, },
        { 0x000ffff8000ffff8ULL, 0x000ffff8000ffff8ULL, },
        { 0xaabd554caabd554cULL, 0xaabd554caabd554cULL, },    /*  16  */
        { 0xaabd554caabd554cULL, 0xaabd554caabd554cULL, },
        { 0xc730e384c730e384ULL, 0xc730e384c730e384ULL, },
        { 0x556aaaa0556aaaa0ULL, 0x556aaaa0556aaaa0ULL, },
        { 0x445bbbb0445bbbb0ULL, 0x445bbbb0445bbbb0ULL, },
        { 0x0017fff40017fff4ULL, 0x0017fff40017fff4ULL, },
        { 0x427812ea0994f678ULL, 0x7b5bda06427812eaULL, },
        { 0xaac55548aac55548ULL, 0xaac55548aac55548ULL, },
        { 0x001bfff2001bfff2ULL, 0x001bfff2001bfff2ULL, },    /*  24  */
        { 0x001bfff2001bfff2ULL, 0x001bfff2001bfff2ULL, },
        { 0x8e55c70e8e55c70eULL, 0x8e55c70e8e55c70eULL, },
        { 0x5572aa9c5572aa9cULL, 0x5572aa9c5572aa9cULL, },
        { 0xcceb3324cceb3324ULL, 0xcceb3324cceb3324ULL, },
        { 0xaac95546aac95546ULL, 0xaac95546aac95546ULL, },
        { 0x4bf95ec12f87d088ULL, 0x686b424f4bf95ec1ULL, },
        { 0x001ffff0001ffff0ULL, 0x001ffff0001ffff0ULL, },
        { 0x6689998866899988ULL, 0x6689998866899988ULL, },    /*  32  */
        { 0x6689998866899988ULL, 0x6689998866899988ULL, },
        { 0x557aaa98557aaa98ULL, 0x557aaa98557aaa98ULL, },
        { 0xccf33320ccf33320ULL, 0xccf33320ccf33320ULL, },
        { 0x8547ae008547ae00ULL, 0x8547ae008547ae00ULL, },
        { 0x335cccb8335cccb8ULL, 0x335cccb8335cccb8ULL, },
        { 0x4fd016ac0b8c5af0ULL, 0x94149f344fd016acULL, },
        { 0x99c6665099c66650ULL, 0x99c6665099c66650ULL, },
        { 0x3360ccb63360ccb6ULL, 0x3360ccb63360ccb6ULL, },    /*  40  */
        { 0x3360ccb63360ccb6ULL, 0x3360ccb63360ccb6ULL, },
        { 0xef1d10faef1d10faULL, 0xef1d10faef1d10faULL, },
        { 0xccfb331cccfb331cULL, 0xccfb331cccfb331cULL, },
        { 0x7b1051d47b1051d4ULL, 0x7b1051d47b1051d4ULL, },
        { 0x6695998266959982ULL, 0x6695998266959982ULL, },
        { 0x2db26bff1ca17d10ULL, 0x3ec38e212db26bffULL, },
        { 0x002fffe8002fffe8ULL, 0x002fffe8002fffe8ULL, },
        { 0xe3c01c598e6b71aeULL, 0x3915c703e3c01c59ULL, },    /*  48  */
        { 0xe3c01c598e6b71aeULL, 0x3915c703e3c01c59ULL, },
        { 0x26202f4f97e86832ULL, 0xb459a11526202f4fULL, },
        { 0xc75038ca1ca6e374ULL, 0x71fb8e1ec75038caULL, },
        { 0xe3c382bef4d671acULL, 0xd2b3609ae3c382beULL, },
        { 0xaae0553baae2553aULL, 0xaae15539aae0553bULL, },
        { 0xd3f7192e919b0636ULL, 0x4f3b0fb0d3f7192eULL, },
        { 0x8e7071ac391dc700ULL, 0xe3c71c548e7071acULL, },
        { 0xaae45539aae65538ULL, 0xaae55537aae45539ULL, },    /*  56  */
        { 0xaae45539aae65538ULL, 0xaae55537aae45539ULL, },
        { 0x133197974c16b408ULL, 0xda4ed07913319797ULL, },
        { 0xc75838c61caee370ULL, 0x72038e1ac75838c6ULL, },
        { 0x114e886aaae8eed0ULL, 0x77b55536114e886aULL, },
        { 0xe3cc1c538e7771a8ULL, 0x3921c6fde3cc1c53ULL, },
        { 0x9e4574d135fa3272ULL, 0xcdadd3a19e4574d1ULL, },
        { 0x003fffe0003fffe0ULL, 0x003fffe0003fffe0ULL, },
        { 0xe77c016cdd7daa5cULL, 0xe98945eb8a373037ULL, },    /*  64  */
        { 0x60fd93dc8ef99616ULL, 0xdba475fe3c075522ULL, },
        { 0x67ae7b204335bcd8ULL, 0xc7121f747860f089ULL, },
        { 0x17bb0f0ee8fd8c96ULL, 0x972fe12d34478253ULL, },
        { 0x913ca17e9a797850ULL, 0x894b1140e617a73eULL, },
        { 0x99ae5a31e83a45a7ULL, 0xff24684bc96dd9adULL, },
        { 0xefeea5a7437774caULL, 0x6ac04bfdaf344b68ULL, },
        { 0x8175363e76faed37ULL, 0xfc38b88657b1e4aaULL, },
        { 0x88261d822b3713f9ULL, 0xe7a661fc940b8011ULL, },    /*  72  */
        { 0xde6668f88674431cULL, 0x534245ae79d1f1ccULL, },
        { 0xf331d070b3932dbbULL, 0xb25f4d6d0200af03ULL, },
        { 0x985e1b88f3e41a1cULL, 0x31e873ed7002566dULL, },
        { 0x486aaf7699abe9daULL, 0x020635a62be8e837ULL, },
        { 0xd9f1400dcd2f6247ULL, 0x937ea22fd4668179ULL, },
        { 0x7f1d8b250d804ea8ULL, 0x1307c8af426828e3ULL, },
        { 0x4be6ff9b22ca7983ULL, 0x7b2e358e09e10fdfULL, },
        { 0x3d0470dbf4d6b86fULL, 0x548567e8f5250450ULL, },    /*  80  */
        { 0x00d897321b41b715ULL, 0x02517c05df66c875ULL, },
        { 0x991ec80ea3b5c306ULL, 0xa18dc9b22cff8e2fULL, },
        { 0x44850796bb133f8dULL, 0xdc2a4cc591614211ULL, },
        { 0x192b30fc8866f607ULL, 0x97e8c289d36e61aaULL, },
        { 0x0058689e9fcad43dULL, 0xfe7a0cc7a239bc40ULL, },
        { 0xb8bc4cc2b8296867ULL, 0xccf01b9e1a7e74adULL, },
        { 0x61014864181c5d2cULL, 0x4c8bc05ea1b0cc11ULL, },
        { 0xec0d0e4af547db74ULL, 0x2d758eed74a13bb5ULL, },    /*  88  */
        { 0x03e797060056a10fULL, 0xc1a1d5f8579892eaULL, },
        { 0x9a3ca5d4a8548905ULL, 0xfd2bfd1807c0081aULL, },
        { 0x4820b48cf1454f6bULL, 0xe982ac5dfb74445aULL, },
        { 0x7eec2fbcb0c3c941ULL, 0x9d1459e9d27d4766ULL, },
        { 0x020a22e0debbd140ULL, 0x4fbb0ef3a9e0453bULL, },
        { 0xe8df4a9ccb0c350bULL, 0x37b3761e2e442cffULL, },
        { 0x7c3604df51731065ULL, 0xd9add64be7d81e17ULL, },
        { 0x35a1aacf3f24481fULL, 0x900caa26ecaf303bULL, },    /*  96  */
        { 0x7f0fd7311d2a2997ULL, 0x5e11155ee03d0362ULL, },
        { 0x7959c1ef0ab6e6c3ULL, 0x41695f03ff01377bULL, },
        { 0x89d8f6a1bc2ded57ULL, 0x29ed46aadb5c8a3cULL, },
        { 0x01ec800ecaa24ac8ULL, 0xf32ccdbb9c58b788ULL, },
        { 0xffd7297c53176782ULL, 0x4acc984953e0cc00ULL, },
        { 0x04316ff6e9707c3dULL, 0xd5f54b0b0ac9f7e0ULL, },
        { 0xffe6fc76421c7405ULL, 0x8f42f98ab98b12e9ULL, },
        { 0xa75ea33ed2e809e1ULL, 0xb6fdbf643abee85cULL, },    /* 104  */
        { 0xc75019063471bcc9ULL, 0x05bcd250f1d0ad42ULL, },
        { 0x300d94eaa78224eaULL, 0x615cfa00370a0c2aULL, },
        { 0xaa1a04f419d03dccULL, 0x8fe0ca60107a1a34ULL, },
        { 0x5f0bb18ad9b000d4ULL, 0xd3ed3780ee630840ULL, },
        { 0x25e24aa388dc4d8cULL, 0x40c1586349788fbaULL, },
        { 0x0ec344de11f41ac8ULL, 0xed9aea2a99a95e8aULL, },
        { 0x02499bebf3ac5a24ULL, 0xecb186c0e06045b8ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPSUB_U_W(b128_pattern[i], b128_pattern[j],
                             b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPSUB_U_W(b128_random[i], b128_random[j],
                             b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                          (PATTERN_INPUTS_SHORT_COUNT)) +
                                         RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPSUB_U_W__DDT(b128_random[i], b128_random[j],
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
            do_msa_DPSUB_U_W__DSD(b128_random[i], b128_random[j],
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
