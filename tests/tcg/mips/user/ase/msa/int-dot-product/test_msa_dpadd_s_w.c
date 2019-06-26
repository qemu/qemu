/*
 *  Test program for MSA instruction DPADD_S.W
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
    char *instruction_name =  "DPADD_S.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000200000002ULL, 0x0000000200000002ULL, },    /*   0  */
        { 0x0000000200000002ULL, 0x0000000200000002ULL, },
        { 0x0000aaae0000aaaeULL, 0x0000aaae0000aaaeULL, },
        { 0x0000000400000004ULL, 0x0000000400000004ULL, },
        { 0x0000666c0000666cULL, 0x0000666c0000666cULL, },
        { 0x0000000600000006ULL, 0x0000000600000006ULL, },
        { 0xffffe39500008e40ULL, 0x000038ebffffe395ULL, },
        { 0x0000000800000008ULL, 0x0000000800000008ULL, },
        { 0x0000000800000008ULL, 0x0000000800000008ULL, },    /*   8  */
        { 0x0000000800000008ULL, 0x0000000800000008ULL, },
        { 0x0000000800000008ULL, 0x0000000800000008ULL, },
        { 0x0000000800000008ULL, 0x0000000800000008ULL, },
        { 0x0000000800000008ULL, 0x0000000800000008ULL, },
        { 0x0000000800000008ULL, 0x0000000800000008ULL, },
        { 0x0000000800000008ULL, 0x0000000800000008ULL, },
        { 0x0000000800000008ULL, 0x0000000800000008ULL, },
        { 0x0000aab40000aab4ULL, 0x0000aab40000aab4ULL, },    /*  16  */
        { 0x0000aab40000aab4ULL, 0x0000aab40000aab4ULL, },
        { 0x38e51c7c38e51c7cULL, 0x38e51c7c38e51c7cULL, },
        { 0x0001556000015560ULL, 0x0001556000015560ULL, },
        { 0x2224445022244450ULL, 0x2224445022244450ULL, },
        { 0x0002000c0002000cULL, 0x0002000c0002000cULL, },
        { 0xf686ed162f6b0988ULL, 0x12f925faf686ed16ULL, },
        { 0x0002aab80002aab8ULL, 0x0002aab80002aab8ULL, },
        { 0x0002000e0002000eULL, 0x0002000e0002000eULL, },    /*  24  */
        { 0x0002000e0002000eULL, 0x0002000e0002000eULL, },
        { 0xc71e38f2c71e38f2ULL, 0xc71e38f2c71e38f2ULL, },
        { 0x0001556400015564ULL, 0x0001556400015564ULL, },
        { 0xdddeccdcdddeccdcULL, 0xdddeccdcdddeccdcULL, },
        { 0x0000aaba0000aabaULL, 0x0000aaba0000aabaULL, },
        { 0x097ba13fd0982f78ULL, 0xed09bdb1097ba13fULL, },
        { 0x0000001000000010ULL, 0x0000001000000010ULL, },
        { 0x0000667800006678ULL, 0x0000667800006678ULL, },    /*  32  */
        { 0x0000667800006678ULL, 0x0000667800006678ULL, },
        { 0x2223556822235568ULL, 0x2223556822235568ULL, },
        { 0x0000cce00000cce0ULL, 0x0000cce00000cce0ULL, },
        { 0x147c5200147c5200ULL, 0x147c5200147c5200ULL, },
        { 0x0001334800013348ULL, 0x0001334800013348ULL, },
        { 0xfa50e9541c73a510ULL, 0x0b6260ccfa50e954ULL, },
        { 0x000199b0000199b0ULL, 0x000199b0000199b0ULL, },
        { 0x0001334a0001334aULL, 0x0001334a0001334aULL, },    /*  40  */
        { 0x0001334a0001334aULL, 0x0001334a0001334aULL, },
        { 0xdddeef06dddeef06ULL, 0xdddeef06dddeef06ULL, },
        { 0x0000cce40000cce4ULL, 0x0000cce40000cce4ULL, },
        { 0xeb85ae2ceb85ae2cULL, 0xeb85ae2ceb85ae2cULL, },
        { 0x0000667e0000667eULL, 0x0000667e0000667eULL, },
        { 0x05b09401e38e82f0ULL, 0xf49f71df05b09401ULL, },
        { 0x0000001800000018ULL, 0x0000001800000018ULL, },
        { 0xffffe3a700008e52ULL, 0x000038fdffffe3a7ULL, },    /*  48  */
        { 0xffffe3a700008e52ULL, 0x000038fdffffe3a7ULL, },
        { 0xf684d0b12f6997ceULL, 0x12f75eebf684d0b1ULL, },
        { 0xffffc73600011c8cULL, 0x000071e2ffffc736ULL, },
        { 0xfa4f7d421c738e54ULL, 0x0b619f66fa4f7d42ULL, },
        { 0xffffaac50001aac6ULL, 0x0000aac7ffffaac5ULL, },
        { 0x0fcce6d235bcf9caULL, 0x3f36f0500fcce6d2ULL, },
        { 0xffff8e5400023900ULL, 0x0000e3acffff8e54ULL, },
        { 0xffffaac70001aac8ULL, 0x0000aac9ffffaac7ULL, },    /*  56  */
        { 0xffffaac70001aac8ULL, 0x0000aac9ffffaac7ULL, },
        { 0x097b6869d0994bf8ULL, 0xed0a2f87097b6869ULL, },
        { 0xffffc73a00011c90ULL, 0x000071e6ffffc73aULL, },
        { 0x05b07796e38f1130ULL, 0xf49faaca05b07796ULL, },
        { 0xffffe3ad00008e58ULL, 0x00003903ffffe3adULL, },
        { 0xf0328b2fca45cd8eULL, 0xc0ca2c5ff0328b2fULL, },
        { 0x0000002000000020ULL, 0x0000002000000020ULL, },
        { 0x3a57fe9422c255a4ULL, 0x16b6ba1518facfc9ULL, },    /*  64  */
        { 0x3c4b6c241c0669eaULL, 0x193d8a02feefaadeULL, },
        { 0x6b6084e0ea284328ULL, 0x2271e08cf3dc0f77ULL, },
        { 0x34b7f0f2ef20736aULL, 0xfb8f1ed3fd8c7dadULL, },
        { 0x36ab5e82e86487b0ULL, 0xfe15eec0e38158c2ULL, },
        { 0x36bda5cf0c93ba59ULL, 0x120897b5002b2653ULL, },
        { 0x38025a59113b8b36ULL, 0x2453b4030525b498ULL, },
        { 0x362cc9c2346212c9ULL, 0x3bf2477af46d1b56ULL, },
        { 0x6541e27e0283ec07ULL, 0x45269e04e9597fefULL, },    /*  72  */
        { 0x66869708072bbce4ULL, 0x5771ba52ee540e34ULL, },
        { 0x9bb32f904f6ed245ULL, 0x6a56b2930fcf50fdULL, },
        { 0x6feae478431ee5e4ULL, 0x731e8c13284ca993ULL, },
        { 0x3942508a48171626ULL, 0x4c3bca5a31fd17c9ULL, },
        { 0x376cbff36b3d9db9ULL, 0x63da5dd121447e87ULL, },
        { 0x0ba474db5eedb158ULL, 0x6ca2375139c1d71dULL, },
        { 0x3edb00658507867dULL, 0xd6e9ca725a84f021ULL, },
        { 0x21746d8f492aab6bULL, 0xc86ec10d5ef05719ULL, },    /*  80  */
        { 0x21105bf47228d8e1ULL, 0xd541f981830d22c5ULL, },
        { 0xf90ba39c64a9aab9ULL, 0xd00d1cd8b17e0558ULL, },
        { 0xedf1ebed93975370ULL, 0xd7fd3855cb7afcd4ULL, },
        { 0xf85b68939e46773eULL, 0xceb49456ccc86662ULL, },
        { 0xf8a465f666205360ULL, 0xe8078ebee9b86012ULL, },
        { 0xdaa6e8fa242ed740ULL, 0xfd8488e8ff04a562ULL, },
        { 0xc84291663638bd8eULL, 0x360ea9ec09bfe9aaULL, },
        { 0xed300e0228a5c87eULL, 0x42280c3610aaee67ULL, },    /*  88  */
        { 0xed8592684150f62dULL, 0x43c5604a0c58a5a1ULL, },
        { 0x1661583a33e11b5dULL, 0x38e0b738fb2ab5fdULL, },
        { 0x27e2359b43cb17c4ULL, 0x4169f958054c48f1ULL, },
        { 0x0ff9c2b35666c87aULL, 0x546263e7ee7c57c1ULL, },
        { 0x0f9e0bba7cf02cdcULL, 0x3fbf94eb097a6841ULL, },
        { 0x06c9e6ca464484ecULL, 0x61838f28157007d3ULL, },
        { 0x0791b5936e65c7d8ULL, 0x6a978c3b0d46a893ULL, },
        { 0x0b5ca2c16d1c8082ULL, 0x84d8b2a628807419ULL, },    /*  96  */
        { 0x0f3c4ea553ddefbaULL, 0x5d23288204008ac5ULL, },
        { 0x006066f95bad42d4ULL, 0x7a5e585328976801ULL, },
        { 0xf610532580647c0eULL, 0xa2551d9f07de4a9aULL, },
        { 0xf65aca543e1e0beaULL, 0x936bdec820b433d4ULL, },
        { 0xf66f1d9c4e4a0274ULL, 0x945159553437f0d0ULL, },
        { 0xf6a34c5265777892ULL, 0x744c4f1e33a0fa19ULL, },
        { 0xf6e8ae026961c977ULL, 0x679ecf7e36000115ULL, },
        { 0x13ee44e6654e7066ULL, 0x828c7150244331b9ULL, },    /* 104  */
        { 0xf787434e16614d78ULL, 0x55caaa201f72a96eULL, },
        { 0xe4e9b290ecfd62e7ULL, 0x76440870087d3a2cULL, },
        { 0x065e2c1ac531b8faULL, 0x86cb35600e1a0d9bULL, },
        { 0x0d00c2eeb7cb8587ULL, 0xa3f3f27b07c3312fULL, },
        { 0x0d62db84ab6f1a84ULL, 0xd3421106ff7d27d5ULL, },
        { 0x10143b76893e48fbULL, 0xdf44d938fb177a2fULL, },
        { 0x1c4ff82055152453ULL, 0xffe7837ceebc407dULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPADD_S_W(b128_pattern[i], b128_pattern[j],
                             b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPADD_S_W(b128_random[i], b128_random[j],
                             b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                          (PATTERN_INPUTS_SHORT_COUNT)) +
                                         RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPADD_S_W__DDT(b128_random[i], b128_random[j],
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
            do_msa_DPADD_S_W__DSD(b128_random[i], b128_random[j],
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
