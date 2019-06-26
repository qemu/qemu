/*
 *  Test program for MSA instruction DPSUB_U.D
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
    char *instruction_name =  "DPSUB_U.D";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x00000003fffffffeULL, 0x00000003fffffffeULL, },    /*   0  */
        { 0x00000003fffffffeULL, 0x00000003fffffffeULL, },
        { 0xaaaaaab155555552ULL, 0xaaaaaab155555552ULL, },
        { 0x00000007fffffffcULL, 0x00000007fffffffcULL, },
        { 0x6666667199999994ULL, 0x6666667199999994ULL, },
        { 0x0000000bfffffffaULL, 0x0000000bfffffffaULL, },
        { 0x8e38e39c71c71c6bULL, 0xe38e38f21c71c715ULL, },
        { 0x0000000ffffffff8ULL, 0x0000000ffffffff8ULL, },
        { 0x0000000ffffffff8ULL, 0x0000000ffffffff8ULL, },    /*   8  */
        { 0x0000000ffffffff8ULL, 0x0000000ffffffff8ULL, },
        { 0x0000000ffffffff8ULL, 0x0000000ffffffff8ULL, },
        { 0x0000000ffffffff8ULL, 0x0000000ffffffff8ULL, },
        { 0x0000000ffffffff8ULL, 0x0000000ffffffff8ULL, },
        { 0x0000000ffffffff8ULL, 0x0000000ffffffff8ULL, },
        { 0x0000000ffffffff8ULL, 0x0000000ffffffff8ULL, },
        { 0x0000000ffffffff8ULL, 0x0000000ffffffff8ULL, },
        { 0xaaaaaabd5555554cULL, 0xaaaaaabd5555554cULL, },    /*  16  */
        { 0xaaaaaabd5555554cULL, 0xaaaaaabd5555554cULL, },
        { 0xc71c71db8e38e384ULL, 0xc71c71db8e38e384ULL, },
        { 0x5555556aaaaaaaa0ULL, 0x5555556aaaaaaaa0ULL, },
        { 0x4444445bbbbbbbb0ULL, 0x4444445bbbbbbbb0ULL, },
        { 0x00000017fffffff4ULL, 0x00000017fffffff4ULL, },
        { 0x097b42784bda12eaULL, 0x425ed0b1684bda06ULL, },
        { 0xaaaaaac555555548ULL, 0xaaaaaac555555548ULL, },
        { 0x0000001bfffffff2ULL, 0x0000001bfffffff2ULL, },    /*  24  */
        { 0x0000001bfffffff2ULL, 0x0000001bfffffff2ULL, },
        { 0x8e38e3ab1c71c70eULL, 0x8e38e3ab1c71c70eULL, },
        { 0x55555572aaaaaa9cULL, 0x55555572aaaaaa9cULL, },
        { 0xcccccceb33333324ULL, 0xcccccceb33333324ULL, },
        { 0xaaaaaac955555546ULL, 0xaaaaaac955555546ULL, },
        { 0x2f684bf97b425ec1ULL, 0x4bda1316097b424fULL, },
        { 0x0000001ffffffff0ULL, 0x0000001ffffffff0ULL, },
        { 0x6666668999999988ULL, 0x6666668999999988ULL, },    /*  32  */
        { 0x6666668999999988ULL, 0x6666668999999988ULL, },
        { 0x5555557aaaaaaa98ULL, 0x5555557aaaaaaa98ULL, },
        { 0xccccccf333333320ULL, 0xccccccf333333320ULL, },
        { 0x851eb87ae147ae00ULL, 0x851eb87ae147ae00ULL, },
        { 0x3333335cccccccb8ULL, 0x3333335cccccccb8ULL, },
        { 0x0b60b636c16c16acULL, 0x4fa4fa7b49f49f34ULL, },
        { 0x999999c666666650ULL, 0x999999c666666650ULL, },
        { 0x33333360ccccccb6ULL, 0x33333360ccccccb6ULL, },    /*  40  */
        { 0x33333360ccccccb6ULL, 0x33333360ccccccb6ULL, },
        { 0xeeeeef1d111110faULL, 0xeeeeef1d111110faULL, },
        { 0xccccccfb3333331cULL, 0xccccccfb3333331cULL, },
        { 0x7ae147dd1eb851d4ULL, 0x7ae147dd1eb851d4ULL, },
        { 0x6666669599999982ULL, 0x6666669599999982ULL, },
        { 0x1c71c74c16c16bffULL, 0x2d82d85d38e38e21ULL, },
        { 0x0000002fffffffe8ULL, 0x0000002fffffffe8ULL, },
        { 0x8e38e3c071c71c59ULL, 0xe38e39161c71c703ULL, },    /*  48  */
        { 0x8e38e3c071c71c59ULL, 0xe38e39161c71c703ULL, },
        { 0x97b42620bda12f4fULL, 0x25ed09af84bda115ULL, },
        { 0x1c71c750e38e38caULL, 0xc71c71fc38e38e1eULL, },
        { 0xf49f4a2ad82d82beULL, 0xe38e391ab60b609aULL, },
        { 0xaaaaaae15555553bULL, 0xaaaaaae255555539ULL, },
        { 0x9161f9e5ba78192eULL, 0xd3c0ca7e948b0fb0ULL, },
        { 0x38e38e71c71c71acULL, 0x8e38e3c871c71c54ULL, },
        { 0xaaaaaae555555539ULL, 0xaaaaaae655555537ULL, },    /*  56  */
        { 0xaaaaaae555555539ULL, 0xaaaaaae655555537ULL, },
        { 0x4bda13325ed09797ULL, 0x12f684fa425ed079ULL, },
        { 0x1c71c758e38e38c6ULL, 0xc71c720438e38e1aULL, },
        { 0xaaaaaae88888886aULL, 0x1111114f55555536ULL, },
        { 0x8e38e3cc71c71c53ULL, 0xe38e39221c71c6fdULL, },
        { 0x35ba78587e6b74d1ULL, 0x9e06526bf9add3a1ULL, },
        { 0x0000003fffffffe0ULL, 0x0000003fffffffe0ULL, },
        { 0xb0ef5df9ed993d50ULL, 0xecd0c902514248ccULL, },    /*  64  */
        { 0x1e8c6aa2f5c2926cULL, 0xd21b7a4e60b25370ULL, },
        { 0xa56477c9bdfa46f4ULL, 0x1c376bca50d3cfdeULL, },
        { 0x5aaf941e1e90da18ULL, 0x4a2661d3ec89a90eULL, },
        { 0xc84ca0c726ba2f34ULL, 0x2f71131ffbf9b3b2ULL, },
        { 0xb93c43f773d898abULL, 0x2c45d9ce74204cfeULL, },
        { 0xd770bf8dbe2e30edULL, 0x1b1d2b640c71ea68ULL, },
        { 0x4c7478e0222edcb6ULL, 0x028c79110abde6f8ULL, },
        { 0xd34c8606ea66913eULL, 0x4ca86a8cfadf6366ULL, },    /*  72  */
        { 0xf181019d34bc2980ULL, 0x3b7fbc22933100d0ULL, },
        { 0xf69966e79904609cULL, 0xc2d94d22461ac53fULL, },
        { 0x669e11492661ba7aULL, 0x3b951b06df919197ULL, },
        { 0x1be92d9d86f84d9eULL, 0x698411107b476ac7ULL, },
        { 0x90ece6efeaf8f967ULL, 0x50f35ebd79936757ULL, },
        { 0x00f1915178565345ULL, 0xc9af2ca2130a33afULL, },
        { 0xad039975a0f53058ULL, 0x0b11d7505015196fULL, },
        { 0x376d4d72ebbc7b1cULL, 0xb833881ecd4918dbULL, },    /*  80  */
        { 0xb97c39c63d30eb26ULL, 0x9983e1a16fddbe3bULL, },
        { 0x103118e687f4c4aaULL, 0x36d2d322776b1540ULL, },
        { 0xd7103f328f5683b0ULL, 0xc97816b7d22d1890ULL, },
        { 0x4dd93b94622edfd8ULL, 0xbd32853a6649bd9eULL, },
        { 0xe38ab03df0d4eedcULL, 0xa6b087fab9ab9432ULL, },
        { 0x9b8bc7cd79738e5aULL, 0x1099960abd7ff844ULL, },
        { 0x2a9e79f404df0445ULL, 0x8a1a574d141add54ULL, },
        { 0x1323c575df66a395ULL, 0x4d70aaa974eb601eULL, },    /*  88  */
        { 0xbc9ea974b0ce57aeULL, 0x3dff93a625e35e6cULL, },
        { 0xbd4cca940103a7a6ULL, 0x1b03e192077feba2ULL, },
        { 0x69e12c9b9ff2608eULL, 0x0713d9101835bf32ULL, },
        { 0x183a0715853e498aULL, 0xeced28ff102b04faULL, },
        { 0xd806808efcdcfa1bULL, 0xda07aee4d9a29bfcULL, },
        { 0x8f0ceb4c5a20614fULL, 0x2693974265c37330ULL, },
        { 0x2f219f4eacacaf61ULL, 0xcde749de29866580ULL, },
        { 0xfac6c540b5ec9bf9ULL, 0x67fa3d30bf85f9fcULL, },    /*  96  */
        { 0x58719a8af58d41b9ULL, 0x8af69bdae8797a8cULL, },
        { 0x0293ed8dc2154481ULL, 0x7aef92fa834de3f0ULL, },
        { 0xe296644d91f354e5ULL, 0xd4332e315ac37ee4ULL, },
        { 0xd78a5344aa8ce0f6ULL, 0xbcf1bf88825a127aULL, },
        { 0xcfe6e77bd50e6bfaULL, 0xa42046c9a6110292ULL, },
        { 0xc2e4e16ef7883199ULL, 0x8a2eb57c71a6b370ULL, },
        { 0xb83af7ab54b68847ULL, 0x7682eb14d9902e98ULL, },
        { 0xfeb58099fb6e2639ULL, 0xd298a4d4f4eef1ccULL, },    /* 104  */
        { 0x9cbae3e8d8c9b31fULL, 0x0e0c2c1a33a56ab0ULL, },
        { 0x95dc4a7a980a468fULL, 0xe95439aa32919b0aULL, },
        { 0xc29c82993429f90bULL, 0xa33308195e2c1fecULL, },
        { 0x5a0a569e52e5f3acULL, 0x0a72368b53acb754ULL, },
        { 0x140968eb707c3bbeULL, 0xcd5491c571071d8cULL, },
        { 0xe1db913744288b2bULL, 0x10c008b6922667d4ULL, },
        { 0x65b190239a38c686ULL, 0xa6d4ec5b01d651c4ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPSUB_U_D(b128_pattern[i], b128_pattern[j],
                             b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPSUB_U_D(b128_random[i], b128_random[j],
                             b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                          (PATTERN_INPUTS_SHORT_COUNT)) +
                                         RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPSUB_U_D__DDT(b128_random[i], b128_random[j],
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
            do_msa_DPSUB_U_D__DSD(b128_random[i], b128_random[j],
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
