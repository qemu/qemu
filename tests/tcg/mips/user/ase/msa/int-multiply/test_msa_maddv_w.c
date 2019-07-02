/*
 *  Test program for MSA instruction MADDV.W
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
    char *instruction_name =  "MADDV.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },    /*   0  */
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },
        { 0x5555555755555557ULL, 0x5555555755555557ULL, },
        { 0x0000000200000002ULL, 0x0000000200000002ULL, },
        { 0x3333333633333336ULL, 0x3333333633333336ULL, },
        { 0x0000000300000003ULL, 0x0000000300000003ULL, },
        { 0x1c71c72071c71c75ULL, 0xc71c71cb1c71c720ULL, },
        { 0x0000000400000004ULL, 0x0000000400000004ULL, },
        { 0x0000000400000004ULL, 0x0000000400000004ULL, },    /*   8  */
        { 0x0000000400000004ULL, 0x0000000400000004ULL, },
        { 0x0000000400000004ULL, 0x0000000400000004ULL, },
        { 0x0000000400000004ULL, 0x0000000400000004ULL, },
        { 0x0000000400000004ULL, 0x0000000400000004ULL, },
        { 0x0000000400000004ULL, 0x0000000400000004ULL, },
        { 0x0000000400000004ULL, 0x0000000400000004ULL, },
        { 0x0000000400000004ULL, 0x0000000400000004ULL, },
        { 0x5555555a5555555aULL, 0x5555555a5555555aULL, },    /*  16  */
        { 0x5555555a5555555aULL, 0x5555555a5555555aULL, },
        { 0x38e38e3e38e38e3eULL, 0x38e38e3e38e38e3eULL, },
        { 0xaaaaaab0aaaaaab0ULL, 0xaaaaaab0aaaaaab0ULL, },
        { 0x2222222822222228ULL, 0x2222222822222228ULL, },
        { 0x0000000600000006ULL, 0x0000000600000006ULL, },
        { 0x12f684c4a12f6852ULL, 0x84bda13612f684c4ULL, },
        { 0x5555555c5555555cULL, 0x5555555c5555555cULL, },
        { 0x0000000700000007ULL, 0x0000000700000007ULL, },    /*  24  */
        { 0x0000000700000007ULL, 0x0000000700000007ULL, },
        { 0x71c71c7971c71c79ULL, 0x71c71c7971c71c79ULL, },
        { 0xaaaaaab2aaaaaab2ULL, 0xaaaaaab2aaaaaab2ULL, },
        { 0x6666666e6666666eULL, 0x6666666e6666666eULL, },
        { 0x5555555d5555555dULL, 0x5555555d5555555dULL, },
        { 0x5ed097bc25ed0983ULL, 0x97b425f55ed097bcULL, },
        { 0x0000000800000008ULL, 0x0000000800000008ULL, },
        { 0x3333333c3333333cULL, 0x3333333c3333333cULL, },    /*  32  */
        { 0x3333333c3333333cULL, 0x3333333c3333333cULL, },
        { 0xaaaaaab4aaaaaab4ULL, 0xaaaaaab4aaaaaab4ULL, },
        { 0x6666667066666670ULL, 0x6666667066666670ULL, },
        { 0x8f5c29008f5c2900ULL, 0x8f5c29008f5c2900ULL, },
        { 0x999999a4999999a4ULL, 0x999999a4999999a4ULL, },
        { 0x7d27d288c16c16ccULL, 0x38e38e447d27d288ULL, },
        { 0xccccccd8ccccccd8ULL, 0xccccccd8ccccccd8ULL, },
        { 0x999999a5999999a5ULL, 0x999999a5999999a5ULL, },    /*  40  */
        { 0x999999a5999999a5ULL, 0x999999a5999999a5ULL, },
        { 0x7777778377777783ULL, 0x7777778377777783ULL, },
        { 0x6666667266666672ULL, 0x6666667266666672ULL, },
        { 0x70a3d71670a3d716ULL, 0x70a3d71670a3d716ULL, },
        { 0x3333333f3333333fULL, 0x3333333f3333333fULL, },
        { 0x6c16c1787d27d289ULL, 0x5b05b0676c16c178ULL, },
        { 0x0000000c0000000cULL, 0x0000000c0000000cULL, },
        { 0x1c71c72971c71c7eULL, 0xc71c71d41c71c729ULL, },    /*  48  */
        { 0x1c71c72971c71c7eULL, 0xc71c71d41c71c729ULL, },
        { 0x2f684be712f684caULL, 0x4bda13042f684be7ULL, },
        { 0x38e38e46e38e38f0ULL, 0x8e38e39c38e38e46ULL, },
        { 0x1c71c72a0b60b618ULL, 0x2d82d83c1c71c72aULL, },
        { 0x5555556355555562ULL, 0x5555556455555563ULL, },
        { 0x0fcd6eac35ba7826ULL, 0x5ba781a40fcd6eacULL, },
        { 0x71c71c80c71c71d4ULL, 0x1c71c72c71c71c80ULL, },
        { 0x5555556455555563ULL, 0x5555556555555564ULL, },    /*  56  */
        { 0x5555556455555563ULL, 0x5555556555555564ULL, },
        { 0x97b425fc097b426dULL, 0x25ed098b97b425fcULL, },
        { 0x38e38e48e38e38f2ULL, 0x8e38e39e38e38e48ULL, },
        { 0x88888898eeeeeefeULL, 0x2222223288888898ULL, },
        { 0x1c71c72c71c71c81ULL, 0xc71c71d71c71c72cULL, },
        { 0x7e6b75000329162fULL, 0x87e6b75f7e6b7500ULL, },
        { 0x0000001000000010ULL, 0x0000001000000010ULL, },
        { 0xb10332a061639010ULL, 0x3a253694749880a0ULL, },    /*  64  */
        { 0xc1c27384487afa10ULL, 0xbb9c0820e3b1a470ULL, },
        { 0x35565efc0caf5a10ULL, 0x735b0ec23bd12160ULL, },
        { 0xe6475258fb27d390ULL, 0x49d49612c9a1c0e0ULL, },
        { 0xf706933ce23f3d90ULL, 0xcb4b679e38bae4b0ULL, },
        { 0xabfab985e02cadd0ULL, 0x0836664283a94cc0ULL, },
        { 0xa33845439e9989d0ULL, 0x5b9fe12897ee3470ULL, },
        { 0x1df3e50abfdd3e40ULL, 0x6d858f1887bc89f0ULL, },
        { 0x9187d08284119e40ULL, 0x254495badfdc06e0ULL, },    /*  72  */
        { 0x88c55c40427e7a40ULL, 0x78ae10a0f420ee90ULL, },
        { 0x3f78e5242782ba40ULL, 0x93ad82a12637b820ULL, },
        { 0x28380a46b1663b40ULL, 0x255be1c9fb128ca0ULL, },
        { 0xd928fda29fdeb4c0ULL, 0xfbd5691988e32c20ULL, },
        { 0x53e49d69c1226930ULL, 0x0dbb170978b181a0ULL, },
        { 0x3ca3c28b4b05ea30ULL, 0x9f6976314d8c5620ULL, },
        { 0x621b15b4fcefb9f4ULL, 0x7f3fac7130ab3a20ULL, },
        { 0x81b8192421043af4ULL, 0x7180d8efde07f3a0ULL, },    /*  80  */
        { 0xa0a1d210b115be94ULL, 0x33a676350e450520ULL, },
        { 0xe27e30b0181b6494ULL, 0x359b330061c70ba0ULL, },
        { 0xe0f1f5a03792b1acULL, 0xe6a63b00d5b18fa0ULL, },
        { 0x38af7120b51538acULL, 0x7938e500aea24b20ULL, },
        { 0x7a4830802390b20cULL, 0x4b472700af547ea0ULL, },
        { 0xcc2f6580204a3c0cULL, 0x37510000bd1b8320ULL, },
        { 0x9ba9ed0066371fb4ULL, 0xeba90000264fb720ULL, },
        { 0x7400c900846dd0b4ULL, 0xb6b700007c524ca0ULL, },    /*  88  */
        { 0x7e4e840000744254ULL, 0xf24d00003540fa20ULL, },
        { 0x242a2c00b0850854ULL, 0xdb00000025db24a0ULL, },
        { 0x38a168005a7bb9ecULL, 0xa3000000566748a0ULL, },
        { 0x6cb048001a7d90ecULL, 0x7d0000000a0cb020ULL, },
        { 0xe4dc2000bd958c4cULL, 0x2f000000f6d44fa0ULL, },
        { 0xbcc9600018fcf64cULL, 0x000000002ecca820ULL, },
        { 0x739b4000140b1974ULL, 0x000000009361fc20ULL, },
        { 0x8ed24000a4acfa74ULL, 0x00000000bcafcda0ULL, },    /*  96  */
        { 0xc3dd40003f7c1b74ULL, 0x00000000fe2b7120ULL, },
        { 0x1fac4000f6087c74ULL, 0x00000000e05abea0ULL, },
        { 0x9e6f40000de21d74ULL, 0x000000001e89ae20ULL, },
        { 0x637500000f743514ULL, 0x00000000c1b755a0ULL, },
        { 0xd9b400005ed469b4ULL, 0x000000005dfcdf20ULL, },
        { 0x0a50000049fea354ULL, 0x000000006b40c2a0ULL, },
        { 0x07400000982609f4ULL, 0x00000000c2b79820ULL, },
        { 0x57c00000b2de7ff4ULL, 0x000000006f6c1aa0ULL, },    /* 104  */
        { 0x1d400000bbe3f5f4ULL, 0x000000001c974f20ULL, },
        { 0x09c00000a8b66bf4ULL, 0x00000000e1391da0ULL, },
        { 0x03400000aed5e1f4ULL, 0x0000000094d78e20ULL, },
        { 0x7d8000009d2e224cULL, 0x00000000ab48a220ULL, },
        { 0x3d000000b40fad74ULL, 0x00000000c40e3620ULL, },
        { 0x96000000332aeeccULL, 0x00000000ecf84a20ULL, },
        { 0xb40000004e1bc8f4ULL, 0x0000000075d6de20ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADDV_W(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADDV_W(b128_random[i], b128_random[j],
                           b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                        (PATTERN_INPUTS_SHORT_COUNT)) +
                                       RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADDV_W__DDT(b128_random[i], b128_random[j],
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
            do_msa_MADDV_W__DSD(b128_random[i], b128_random[j],
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
