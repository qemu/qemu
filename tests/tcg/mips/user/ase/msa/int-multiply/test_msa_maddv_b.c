/*
 *  Test program for MSA instruction MADDV.B
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
    char *group_name = "Int Multiply";
    char *instruction_name =  "MADDV.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },    /*   0  */
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0x5757575757575757ULL, 0x5757575757575757ULL, },
        { 0x0202020202020202ULL, 0x0202020202020202ULL, },
        { 0x3636363636363636ULL, 0x3636363636363636ULL, },
        { 0x0303030303030303ULL, 0x0303030303030303ULL, },
        { 0x2075cb2075cb2075ULL, 0xcb2075cb2075cb20ULL, },
        { 0x0404040404040404ULL, 0x0404040404040404ULL, },
        { 0x0404040404040404ULL, 0x0404040404040404ULL, },    /*   8  */
        { 0x0404040404040404ULL, 0x0404040404040404ULL, },
        { 0x0404040404040404ULL, 0x0404040404040404ULL, },
        { 0x0404040404040404ULL, 0x0404040404040404ULL, },
        { 0x0404040404040404ULL, 0x0404040404040404ULL, },
        { 0x0404040404040404ULL, 0x0404040404040404ULL, },
        { 0x0404040404040404ULL, 0x0404040404040404ULL, },
        { 0x0404040404040404ULL, 0x0404040404040404ULL, },
        { 0x5a5a5a5a5a5a5a5aULL, 0x5a5a5a5a5a5a5a5aULL, },    /*  16  */
        { 0x5a5a5a5a5a5a5a5aULL, 0x5a5a5a5a5a5a5a5aULL, },
        { 0x3e3e3e3e3e3e3e3eULL, 0x3e3e3e3e3e3e3e3eULL, },
        { 0xb0b0b0b0b0b0b0b0ULL, 0xb0b0b0b0b0b0b0b0ULL, },
        { 0x2828282828282828ULL, 0x2828282828282828ULL, },
        { 0x0606060606060606ULL, 0x0606060606060606ULL, },
        { 0xc45236c45236c452ULL, 0x36c45236c45236c4ULL, },
        { 0x5c5c5c5c5c5c5c5cULL, 0x5c5c5c5c5c5c5c5cULL, },
        { 0x0707070707070707ULL, 0x0707070707070707ULL, },    /*  24  */
        { 0x0707070707070707ULL, 0x0707070707070707ULL, },
        { 0x7979797979797979ULL, 0x7979797979797979ULL, },
        { 0xb2b2b2b2b2b2b2b2ULL, 0xb2b2b2b2b2b2b2b2ULL, },
        { 0x6e6e6e6e6e6e6e6eULL, 0x6e6e6e6e6e6e6e6eULL, },
        { 0x5d5d5d5d5d5d5d5dULL, 0x5d5d5d5d5d5d5d5dULL, },
        { 0xbc83f5bc83f5bc83ULL, 0xf5bc83f5bc83f5bcULL, },
        { 0x0808080808080808ULL, 0x0808080808080808ULL, },
        { 0x3c3c3c3c3c3c3c3cULL, 0x3c3c3c3c3c3c3c3cULL, },    /*  32  */
        { 0x3c3c3c3c3c3c3c3cULL, 0x3c3c3c3c3c3c3c3cULL, },
        { 0xb4b4b4b4b4b4b4b4ULL, 0xb4b4b4b4b4b4b4b4ULL, },
        { 0x7070707070707070ULL, 0x7070707070707070ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xa4a4a4a4a4a4a4a4ULL, 0xa4a4a4a4a4a4a4a4ULL, },
        { 0x88cc4488cc4488ccULL, 0x4488cc4488cc4488ULL, },
        { 0xd8d8d8d8d8d8d8d8ULL, 0xd8d8d8d8d8d8d8d8ULL, },
        { 0xa5a5a5a5a5a5a5a5ULL, 0xa5a5a5a5a5a5a5a5ULL, },    /*  40  */
        { 0xa5a5a5a5a5a5a5a5ULL, 0xa5a5a5a5a5a5a5a5ULL, },
        { 0x8383838383838383ULL, 0x8383838383838383ULL, },
        { 0x7272727272727272ULL, 0x7272727272727272ULL, },
        { 0x1616161616161616ULL, 0x1616161616161616ULL, },
        { 0x3f3f3f3f3f3f3f3fULL, 0x3f3f3f3f3f3f3f3fULL, },
        { 0x7889677889677889ULL, 0x6778896778896778ULL, },
        { 0x0c0c0c0c0c0c0c0cULL, 0x0c0c0c0c0c0c0c0cULL, },
        { 0x297ed4297ed4297eULL, 0xd4297ed4297ed429ULL, },    /*  48  */
        { 0x297ed4297ed4297eULL, 0xd4297ed4297ed429ULL, },
        { 0xe7ca04e7ca04e7caULL, 0x04e7ca04e7ca04e7ULL, },
        { 0x46f09c46f09c46f0ULL, 0x9c46f09c46f09c46ULL, },
        { 0x2a183c2a183c2a18ULL, 0x3c2a183c2a183c2aULL, },
        { 0x6362646362646362ULL, 0x6463626463626463ULL, },
        { 0xac26a4ac26a4ac26ULL, 0xa4ac26a4ac26a4acULL, },
        { 0x80d42c80d42c80d4ULL, 0x2c80d42c80d42c80ULL, },
        { 0x6463656463656463ULL, 0x6564636564636564ULL, },    /*  56  */
        { 0x6463656463656463ULL, 0x6564636564636564ULL, },
        { 0xfc6d8bfc6d8bfc6dULL, 0x8bfc6d8bfc6d8bfcULL, },
        { 0x48f29e48f29e48f2ULL, 0x9e48f29e48f29e48ULL, },
        { 0x98fe3298fe3298feULL, 0x3298fe3298fe3298ULL, },
        { 0x2c81d72c81d72c81ULL, 0xd72c81d72c81d72cULL, },
        { 0x002f5f002f5f002fULL, 0x5f002f5f002f5f00ULL, },
        { 0x1010101010101010ULL, 0x1010101010101010ULL, },
        { 0x50f4b4a050944910ULL, 0x09818994142910a0ULL, },    /*  64  */
        { 0xa8a0b48458da5c10ULL, 0x4fe29220ea6e7070ULL, },
        { 0x08e408fc40188310ULL, 0xbcca14c29417e060ULL, },
        { 0x889acc58f0da8d90ULL, 0x0bc1ec1242cd40e0ULL, },
        { 0xe046cc3cf820a090ULL, 0x5122f59e1812a0b0ULL, },
        { 0xf94acc85218951d0ULL, 0x95738e42d193e4c0ULL, },
        { 0x9d16cc43c6665ed0ULL, 0x53db3028d828be70ULL, },
        { 0x6db8cc0a0c890c40ULL, 0x3d628818b56622f0ULL, },
        { 0xcdfc2082f4c73340ULL, 0xaa4a0aba5f0f92e0ULL, },    /*  72  */
        { 0x71c8204099a44040ULL, 0x68b2aca066a46c90ULL, },
        { 0x016c64244a05b940ULL, 0x59f2d0a19fddc520ULL, },
        { 0x4132584638a46f40ULL, 0xd44a00c982f36fa0ULL, },
        { 0xc1e81ca2e86679c0ULL, 0x2341d81930a9cf20ULL, },
        { 0x918a1c692e892730ULL, 0x0dc830090de733a0ULL, },
        { 0xd150108b1c28dd30ULL, 0x88206031f0fddd20ULL, },
        { 0xd1b1f4b4a08961f4ULL, 0x3101a07181016120ULL, },
        { 0xd9fb2c24a0fb96f4ULL, 0x8c6880ef7f7c11a0ULL, },    /*  80  */
        { 0x9c452c10c01c3094ULL, 0x64c00035ea008320ULL, },
        { 0x6c8714b080c04094ULL, 0xa0c00000380072a0ULL, },
        { 0xac30cca08080c0acULL, 0xc0800000300016a0ULL, },
        { 0x0c101420808080acULL, 0x00000000d0003620ULL, },
        { 0xd0f014800000000cULL, 0x00000000e00082a0ULL, },
        { 0x9050ac800000000cULL, 0x0000000080004c20ULL, },
        { 0x90007400000000b4ULL, 0x0000000000006420ULL, },
        { 0x1000ac00000000b4ULL, 0x00000000000024a0ULL, },    /*  88  */
        { 0xc000ac0000000054ULL, 0x000000000000ac20ULL, },
        { 0xc000940000000054ULL, 0x00000000000088a0ULL, },
        { 0xc0004c00000000ecULL, 0x00000000000098a0ULL, },
        { 0xc0009400000000ecULL, 0x0000000000001820ULL, },
        { 0x000094000000004cULL, 0x000000000000c8a0ULL, },
        { 0x00002c000000004cULL, 0x000000000000b020ULL, },
        { 0x0000f40000000074ULL, 0x0000000000001020ULL, },
        { 0x00002c0000000074ULL, 0x00000000000010a0ULL, },    /*  96  */
        { 0x0000b40000000074ULL, 0x0000000000001020ULL, },
        { 0x00006c0000000074ULL, 0x00000000000010a0ULL, },
        { 0x0000740000000074ULL, 0x0000000000001020ULL, },
        { 0x0000740000000014ULL, 0x00000000000030a0ULL, },
        { 0x00007400000000b4ULL, 0x0000000000009020ULL, },
        { 0x0000740000000054ULL, 0x000000000000b0a0ULL, },
        { 0x00007400000000f4ULL, 0x0000000000001020ULL, },
        { 0x00004c00000000f4ULL, 0x00000000000060a0ULL, },    /* 104  */
        { 0x0000f400000000f4ULL, 0x0000000000004020ULL, },
        { 0x0000cc00000000f4ULL, 0x00000000000080a0ULL, },
        { 0x00007400000000f4ULL, 0x0000000000000020ULL, },
        { 0x00006c000000004cULL, 0x0000000000000020ULL, },
        { 0x0000b40000000074ULL, 0x0000000000000020ULL, },
        { 0x00002c00000000ccULL, 0x0000000000000020ULL, },
        { 0x0000f400000000f4ULL, 0x0000000000000020ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADDV_B(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADDV_B(b128_random[i], b128_random[j],
                           b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                        (PATTERN_INPUTS_SHORT_COUNT)) +
                                       RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADDV_B__DDT(b128_random[i], b128_random[j],
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
            do_msa_MADDV_B__DSD(b128_random[i], b128_random[j],
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
