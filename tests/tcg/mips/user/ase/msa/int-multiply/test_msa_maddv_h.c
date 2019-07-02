/*
 *  Test program for MSA instruction MADDV.H
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
    char *instruction_name =  "MADDV.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },    /*   0  */
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },
        { 0x5557555755575557ULL, 0x5557555755575557ULL, },
        { 0x0002000200020002ULL, 0x0002000200020002ULL, },
        { 0x3336333633363336ULL, 0x3336333633363336ULL, },
        { 0x0003000300030003ULL, 0x0003000300030003ULL, },
        { 0x1c75c72071cb1c75ULL, 0xc72071cb1c75c720ULL, },
        { 0x0004000400040004ULL, 0x0004000400040004ULL, },
        { 0x0004000400040004ULL, 0x0004000400040004ULL, },    /*   8  */
        { 0x0004000400040004ULL, 0x0004000400040004ULL, },
        { 0x0004000400040004ULL, 0x0004000400040004ULL, },
        { 0x0004000400040004ULL, 0x0004000400040004ULL, },
        { 0x0004000400040004ULL, 0x0004000400040004ULL, },
        { 0x0004000400040004ULL, 0x0004000400040004ULL, },
        { 0x0004000400040004ULL, 0x0004000400040004ULL, },
        { 0x0004000400040004ULL, 0x0004000400040004ULL, },
        { 0x555a555a555a555aULL, 0x555a555a555a555aULL, },    /*  16  */
        { 0x555a555a555a555aULL, 0x555a555a555a555aULL, },
        { 0x8e3e8e3e8e3e8e3eULL, 0x8e3e8e3e8e3e8e3eULL, },
        { 0xaab0aab0aab0aab0ULL, 0xaab0aab0aab0aab0ULL, },
        { 0x2228222822282228ULL, 0x2228222822282228ULL, },
        { 0x0006000600060006ULL, 0x0006000600060006ULL, },
        { 0x685284c4a1366852ULL, 0x84c4a136685284c4ULL, },
        { 0x555c555c555c555cULL, 0x555c555c555c555cULL, },
        { 0x0007000700070007ULL, 0x0007000700070007ULL, },    /*  24  */
        { 0x0007000700070007ULL, 0x0007000700070007ULL, },
        { 0x1c791c791c791c79ULL, 0x1c791c791c791c79ULL, },
        { 0xaab2aab2aab2aab2ULL, 0xaab2aab2aab2aab2ULL, },
        { 0x666e666e666e666eULL, 0x666e666e666e666eULL, },
        { 0x555d555d555d555dULL, 0x555d555d555d555dULL, },
        { 0x098397bc25f50983ULL, 0x97bc25f5098397bcULL, },
        { 0x0008000800080008ULL, 0x0008000800080008ULL, },
        { 0x333c333c333c333cULL, 0x333c333c333c333cULL, },    /*  32  */
        { 0x333c333c333c333cULL, 0x333c333c333c333cULL, },
        { 0xaab4aab4aab4aab4ULL, 0xaab4aab4aab4aab4ULL, },
        { 0x6670667066706670ULL, 0x6670667066706670ULL, },
        { 0x2900290029002900ULL, 0x2900290029002900ULL, },
        { 0x99a499a499a499a4ULL, 0x99a499a499a499a4ULL, },
        { 0x16ccd2888e4416ccULL, 0xd2888e4416ccd288ULL, },
        { 0xccd8ccd8ccd8ccd8ULL, 0xccd8ccd8ccd8ccd8ULL, },
        { 0x99a599a599a599a5ULL, 0x99a599a599a599a5ULL, },    /*  40  */
        { 0x99a599a599a599a5ULL, 0x99a599a599a599a5ULL, },
        { 0x7783778377837783ULL, 0x7783778377837783ULL, },
        { 0x6672667266726672ULL, 0x6672667266726672ULL, },
        { 0xd716d716d716d716ULL, 0xd716d716d716d716ULL, },
        { 0x333f333f333f333fULL, 0x333f333f333f333fULL, },
        { 0xd289c178b067d289ULL, 0xc178b067d289c178ULL, },
        { 0x000c000c000c000cULL, 0x000c000c000c000cULL, },
        { 0x1c7ec72971d41c7eULL, 0xc72971d41c7ec729ULL, },    /*  48  */
        { 0x1c7ec72971d41c7eULL, 0xc72971d41c7ec729ULL, },
        { 0x84ca4be7130484caULL, 0x4be7130484ca4be7ULL, },
        { 0x38f08e46e39c38f0ULL, 0x8e46e39c38f08e46ULL, },
        { 0xb618c72ad83cb618ULL, 0xc72ad83cb618c72aULL, },
        { 0x5562556355645562ULL, 0x5563556455625563ULL, },
        { 0x78266eac81a47826ULL, 0x6eac81a478266eacULL, },
        { 0x71d41c80c72c71d4ULL, 0x1c80c72c71d41c80ULL, },
        { 0x5563556455655563ULL, 0x5564556555635564ULL, },    /*  56  */
        { 0x5563556455655563ULL, 0x5564556555635564ULL, },
        { 0x426d25fc098b426dULL, 0x25fc098b426d25fcULL, },
        { 0x38f28e48e39e38f2ULL, 0x8e48e39e38f28e48ULL, },
        { 0xeefe88982232eefeULL, 0x88982232eefe8898ULL, },
        { 0x1c81c72c71d71c81ULL, 0xc72c71d71c81c72cULL, },
        { 0x162f7500b75f162fULL, 0x7500b75f162f7500ULL, },
        { 0x0010001000100010ULL, 0x0010001000100010ULL, },
        { 0xcbf432a0c5949010ULL, 0x838136944f2980a0ULL, },    /*  64  */
        { 0xf8a073846fdafa10ULL, 0x81e20820066ea470ULL, },
        { 0x25e45efce9185a10ULL, 0xd1ca0ec2ee172160ULL, },
        { 0x9e9a52589fdad390ULL, 0x88c19612bccdc0e0ULL, },
        { 0xcb46933c4a203d90ULL, 0x8722679e7412e4b0ULL, },
        { 0xec4ab9850c89add0ULL, 0x31736642d9934cc0ULL, },
        { 0x15164543016689d0ULL, 0xd2dbe12880283470ULL, },
        { 0xe4b8e50ad4893e40ULL, 0xb8628f18916689f0ULL, },
        { 0x11fcd0824dc79e40ULL, 0x084a95ba790f06e0ULL, },    /*  72  */
        { 0x3ac85c4042a47a40ULL, 0xa9b210a01fa4ee90ULL, },
        { 0x4a6ce5241805ba40ULL, 0x2ff282a198ddb820ULL, },
        { 0xda320a46aaa43b40ULL, 0xaa4ae1c91cf38ca0ULL, },
        { 0x52e8fda26166b4c0ULL, 0x61416919eba92c20ULL, },
        { 0x228a9d6934896930ULL, 0x46c81709fce781a0ULL, },
        { 0xb250c28bc728ea30ULL, 0xc120763180fd5620ULL, },
        { 0xeab115b4cc89b9f4ULL, 0x1e01ac71b6013a20ULL, },
        { 0x1ffb192480fb3af4ULL, 0x7b68d8ef267cf3a0ULL, },    /*  80  */
        { 0xf545d210101cbe94ULL, 0xdcc07635cb000520ULL, },
        { 0x8b8730b052c06494ULL, 0x5ec03300e4000ba0ULL, },
        { 0xaa30f5a0a980b1acULL, 0x51803b00ac008fa0ULL, },
        { 0xa21071208c8038acULL, 0x9c00e50050004b20ULL, },
        { 0x99f03080ba00b20cULL, 0x2000270000007ea0ULL, },
        { 0xf850658020003c0cULL, 0x2000000000008320ULL, },
        { 0x9900ed0040001fb4ULL, 0x400000000000b720ULL, },
        { 0xf300c900c000d0b4ULL, 0x0000000000004ca0ULL, },    /*  88  */
        { 0x4d00840000004254ULL, 0x000000000000fa20ULL, },
        { 0x5f002c0000000854ULL, 0x00000000000024a0ULL, },
        { 0xb00068000000b9ecULL, 0x00000000000048a0ULL, },
        { 0x90004800000090ecULL, 0x000000000000b020ULL, },
        { 0x7000200000008c4cULL, 0x0000000000004fa0ULL, },
        { 0xd00060000000f64cULL, 0x000000000000a820ULL, },
        { 0x0000400000001974ULL, 0x000000000000fc20ULL, },
        { 0x000040000000fa74ULL, 0x000000000000cda0ULL, },    /*  96  */
        { 0x0000400000001b74ULL, 0x0000000000007120ULL, },
        { 0x0000400000007c74ULL, 0x000000000000bea0ULL, },
        { 0x0000400000001d74ULL, 0x000000000000ae20ULL, },
        { 0x0000000000003514ULL, 0x00000000000055a0ULL, },
        { 0x00000000000069b4ULL, 0x000000000000df20ULL, },
        { 0x000000000000a354ULL, 0x000000000000c2a0ULL, },
        { 0x00000000000009f4ULL, 0x0000000000009820ULL, },
        { 0x0000000000007ff4ULL, 0x0000000000001aa0ULL, },    /* 104  */
        { 0x000000000000f5f4ULL, 0x0000000000004f20ULL, },
        { 0x0000000000006bf4ULL, 0x0000000000001da0ULL, },
        { 0x000000000000e1f4ULL, 0x0000000000008e20ULL, },
        { 0x000000000000224cULL, 0x000000000000a220ULL, },
        { 0x000000000000ad74ULL, 0x0000000000003620ULL, },
        { 0x000000000000eeccULL, 0x0000000000004a20ULL, },
        { 0x000000000000c8f4ULL, 0x000000000000de20ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADDV_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADDV_H(b128_random[i], b128_random[j],
                           b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                        (PATTERN_INPUTS_SHORT_COUNT)) +
                                       RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADDV_H__DDT(b128_random[i], b128_random[j],
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
            do_msa_MADDV_H__DSD(b128_random[i], b128_random[j],
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
