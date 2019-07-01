/*
 *  Test program for MSA instruction DPSUB_S.D
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
    char *instruction_name =  "DPSUB_S.D";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xfffffffffffffffeULL, 0xfffffffffffffffeULL, },    /*   0  */
        { 0xfffffffffffffffeULL, 0xfffffffffffffffeULL, },
        { 0xffffffff55555552ULL, 0xffffffff55555552ULL, },
        { 0xfffffffffffffffcULL, 0xfffffffffffffffcULL, },
        { 0xffffffff99999994ULL, 0xffffffff99999994ULL, },
        { 0xfffffffffffffffaULL, 0xfffffffffffffffaULL, },
        { 0xffffffff71c71c6bULL, 0x000000001c71c715ULL, },
        { 0xfffffffffffffff8ULL, 0xfffffffffffffff8ULL, },
        { 0xfffffffffffffff8ULL, 0xfffffffffffffff8ULL, },    /*   8  */
        { 0xfffffffffffffff8ULL, 0xfffffffffffffff8ULL, },
        { 0xfffffffffffffff8ULL, 0xfffffffffffffff8ULL, },
        { 0xfffffffffffffff8ULL, 0xfffffffffffffff8ULL, },
        { 0xfffffffffffffff8ULL, 0xfffffffffffffff8ULL, },
        { 0xfffffffffffffff8ULL, 0xfffffffffffffff8ULL, },
        { 0xfffffffffffffff8ULL, 0xfffffffffffffff8ULL, },
        { 0xfffffffffffffff8ULL, 0xfffffffffffffff8ULL, },
        { 0xffffffff5555554cULL, 0xffffffff5555554cULL, },    /*  16  */
        { 0xffffffff5555554cULL, 0xffffffff5555554cULL, },
        { 0xc71c71c58e38e384ULL, 0xc71c71c58e38e384ULL, },
        { 0xfffffffeaaaaaaa0ULL, 0xfffffffeaaaaaaa0ULL, },
        { 0xdddddddbbbbbbbb0ULL, 0xdddddddbbbbbbbb0ULL, },
        { 0xfffffffdfffffff4ULL, 0xfffffffdfffffff4ULL, },
        { 0xd097b4234bda12eaULL, 0x097b425c684bda06ULL, },
        { 0xfffffffd55555548ULL, 0xfffffffd55555548ULL, },
        { 0xfffffffdfffffff2ULL, 0xfffffffdfffffff2ULL, },    /*  24  */
        { 0xfffffffdfffffff2ULL, 0xfffffffdfffffff2ULL, },
        { 0x38e38e371c71c70eULL, 0x38e38e371c71c70eULL, },
        { 0xfffffffeaaaaaa9cULL, 0xfffffffeaaaaaa9cULL, },
        { 0x2222222133333324ULL, 0x2222222133333324ULL, },
        { 0xffffffff55555546ULL, 0xffffffff55555546ULL, },
        { 0x2f684bd97b425ec1ULL, 0xf684bda1097b424fULL, },
        { 0xfffffffffffffff0ULL, 0xfffffffffffffff0ULL, },
        { 0xffffffff99999988ULL, 0xffffffff99999988ULL, },    /*  32  */
        { 0xffffffff99999988ULL, 0xffffffff99999988ULL, },
        { 0xdddddddcaaaaaa98ULL, 0xdddddddcaaaaaa98ULL, },
        { 0xffffffff33333320ULL, 0xffffffff33333320ULL, },
        { 0xeb851eb6e147ae00ULL, 0xeb851eb6e147ae00ULL, },
        { 0xfffffffeccccccb8ULL, 0xfffffffeccccccb8ULL, },
        { 0xe38e38e1c16c16acULL, 0x05b05b0449f49f34ULL, },
        { 0xfffffffe66666650ULL, 0xfffffffe66666650ULL, },
        { 0xfffffffeccccccb6ULL, 0xfffffffeccccccb6ULL, },    /*  40  */
        { 0xfffffffeccccccb6ULL, 0xfffffffeccccccb6ULL, },
        { 0x22222221111110faULL, 0x22222221111110faULL, },
        { 0xffffffff3333331cULL, 0xffffffff3333331cULL, },
        { 0x147ae1471eb851d4ULL, 0x147ae1471eb851d4ULL, },
        { 0xffffffff99999982ULL, 0xffffffff99999982ULL, },
        { 0x1c71c71c16c16bffULL, 0xfa4fa4fa38e38e21ULL, },
        { 0xffffffffffffffe8ULL, 0xffffffffffffffe8ULL, },
        { 0xffffffff71c71c59ULL, 0x000000001c71c703ULL, },    /*  48  */
        { 0xffffffff71c71c59ULL, 0x000000001c71c703ULL, },
        { 0xd097b424bda12f4fULL, 0x097b425e84bda115ULL, },
        { 0xfffffffee38e38caULL, 0x0000000038e38e1eULL, },
        { 0xe38e38e1d82d82beULL, 0x05b05b05b60b609aULL, },
        { 0xfffffffe5555553bULL, 0x0000000055555539ULL, },
        { 0xca4587e4ba78192eULL, 0xf0329162948b0fb0ULL, },
        { 0xfffffffdc71c71acULL, 0x0000000071c71c54ULL, },
        { 0xfffffffe55555539ULL, 0x0000000055555537ULL, },    /*  56  */
        { 0xfffffffe55555539ULL, 0x0000000055555537ULL, },
        { 0x2f684bd85ed09797ULL, 0xf684bda1425ed079ULL, },
        { 0xfffffffee38e38c6ULL, 0x0000000038e38e1aULL, },
        { 0x1c71c71b8888886aULL, 0xfa4fa4fa55555536ULL, },
        { 0xffffffff71c71c53ULL, 0x000000001c71c6fdULL, },
        { 0x35ba78187e6b74d1ULL, 0x0fcd6e9df9add3a1ULL, },
        { 0xffffffffffffffe0ULL, 0xffffffffffffffe0ULL, },
        { 0xc1c52b51ed993d50ULL, 0xe9c828da514248ccULL, },    /*  64  */
        { 0xb38b1f29f5c2926cULL, 0xe4522d2260b25370ULL, },
        { 0x978b1706bdfa46f4ULL, 0xd814f3be50d3cfdeULL, },
        { 0xbd2549a81e90da18ULL, 0xf92987d1ec89a90eULL, },
        { 0xaeeb3d8026ba2f34ULL, 0xf3b38c19fbf9b3b2ULL, },
        { 0x9756e17673d898abULL, 0xf08852c874204cfeULL, },
        { 0xab37d321be2e30edULL, 0xf49ef75a0c71ea68ULL, },
        { 0x908aa2c1222edcb6ULL, 0x0445531d0abde6f8ULL, },
        { 0x748a9a9dea66913eULL, 0xf80819b8fadf6366ULL, },    /*  72  */
        { 0x886b8c4934bc2980ULL, 0xfc1ebe4a933100d0ULL, },
        { 0x59d865e79904609cULL, 0xd9ce9972461ac53fULL, },
        { 0x985e08e42661ba7aULL, 0xced13609df919197ULL, },
        { 0xbdf83b8586f84d9eULL, 0xefe5ca1d7b476ac7ULL, },
        { 0xa34b0b24eaf8f967ULL, 0xff8c25e079936757ULL, },
        { 0xe1d0ae2178565345ULL, 0xf48ec278130a33afULL, },
        { 0x8de2b645a0f53058ULL, 0xa45a44165015196fULL, },
        { 0x6792d4f3d7eea55cULL, 0xbfd22ee1a25aa627ULL, },    /*  80  */
        { 0x75702d5b9af89c83ULL, 0xcc593d1da09f7be9ULL, },
        { 0x801c3e1c97724195ULL, 0xb4c868d4067dd2d2ULL, },
        { 0xdeafd0d6f0bea5c3ULL, 0x957877eb733b98b2ULL, },
        { 0xd1883629f50ec77bULL, 0xb587d85cf1ffef10ULL, },
        { 0xd4133b37d7cbfcc8ULL, 0xbc35d373b6f24df8ULL, },
        { 0xbab344ed957a4c42ULL, 0xae8dcb499ce6cd0bULL, },
        { 0x004c193eb947b2ddULL, 0x68b0a9907b71a293ULL, },
        { 0x0b979b74995fc935ULL, 0x4a9602f12aa080cfULL, },    /*  88  */
        { 0x2ae2653846d12eb1ULL, 0x4185939a2d850f91ULL, },
        { 0x4c5017cc0eed7401ULL, 0x466840b4575dc0d7ULL, },
        { 0x255760c7e1e38957ULL, 0x8360b1037a4f3497ULL, },
        { 0x3b88c1c3a41f6803ULL, 0xa8cf0d07b592cd69ULL, },
        { 0x585dd51272f3e482ULL, 0xb5723c3756218857ULL, },
        { 0x94c1c43b5f5b538eULL, 0xdd9794c5786cc9c2ULL, },
        { 0xa0b80278cc3c6a8bULL, 0xf710a53506ea3e4aULL, },
        { 0x7c607ecd0201d92bULL, 0xf9bcdab0e105825cULL, },    /*  96  */
        { 0xb628bad7d2470e0fULL, 0xfb660e974362496cULL, },
        { 0x9ae11df599c281fbULL, 0xfd2738784b8dbfeaULL, },
        { 0x7bc5bf3b5e23aeffULL, 0xfe707ab5676dfce2ULL, },
        { 0x614dabb2dc4e0a36ULL, 0xf5f8795b76d8fd08ULL, },
        { 0x6dbd1a209fc658b0ULL, 0xecd982bc128c8ceaULL, },
        { 0x8cb93c5d61b1a8d0ULL, 0xecbaa1839f7e477aULL, },
        { 0x6d33947e52d25a59ULL, 0xf62aab8428f0bf14ULL, },
        { 0xa7970469e4259b2dULL, 0x0543881aad9efd08ULL, },    /* 104  */
        { 0x8310e5e55f8149f3ULL, 0xe925758a04d06282ULL, },
        { 0x746e208dd13c0f61ULL, 0xee4c7bccbccd15e4ULL, },
        { 0x8da69743b598403fULL, 0xdac93db8514253e0ULL, },
        { 0xdb31a0aea0a5cde6ULL, 0xe5bd105b853454a0ULL, },
        { 0x0e6cfc3a89e7bd7cULL, 0xb06ea3bad3a90bd8ULL, },
        { 0x338cc47438edb042ULL, 0x7df572596f6dffe8ULL, },
        { 0x07fce3091840a942ULL, 0xdbd5224936527bd0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPSUB_S_D(b128_pattern[i], b128_pattern[j],
                             b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPSUB_S_D(b128_random[i], b128_random[j],
                             b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                          (PATTERN_INPUTS_SHORT_COUNT)) +
                                         RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPSUB_S_D__DDT(b128_random[i], b128_random[j],
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
            do_msa_DPSUB_S_D__DSD(b128_random[i], b128_random[j],
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
