/*
 *  Test program for MSA instruction MADDV.D
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
    char *instruction_name =  "MADDV.D";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000001ULL, 0x0000000000000001ULL, },    /*   0  */
        { 0x0000000000000001ULL, 0x0000000000000001ULL, },
        { 0x5555555555555557ULL, 0x5555555555555557ULL, },
        { 0x0000000000000002ULL, 0x0000000000000002ULL, },
        { 0x3333333333333336ULL, 0x3333333333333336ULL, },
        { 0x0000000000000003ULL, 0x0000000000000003ULL, },
        { 0x1c71c71c71c71c75ULL, 0xc71c71c71c71c720ULL, },
        { 0x0000000000000004ULL, 0x0000000000000004ULL, },
        { 0x0000000000000004ULL, 0x0000000000000004ULL, },    /*   8  */
        { 0x0000000000000004ULL, 0x0000000000000004ULL, },
        { 0x0000000000000004ULL, 0x0000000000000004ULL, },
        { 0x0000000000000004ULL, 0x0000000000000004ULL, },
        { 0x0000000000000004ULL, 0x0000000000000004ULL, },
        { 0x0000000000000004ULL, 0x0000000000000004ULL, },
        { 0x0000000000000004ULL, 0x0000000000000004ULL, },
        { 0x0000000000000004ULL, 0x0000000000000004ULL, },
        { 0x555555555555555aULL, 0x555555555555555aULL, },    /*  16  */
        { 0x555555555555555aULL, 0x555555555555555aULL, },
        { 0x8e38e38e38e38e3eULL, 0x8e38e38e38e38e3eULL, },
        { 0xaaaaaaaaaaaaaab0ULL, 0xaaaaaaaaaaaaaab0ULL, },
        { 0x2222222222222228ULL, 0x2222222222222228ULL, },
        { 0x0000000000000006ULL, 0x0000000000000006ULL, },
        { 0x12f684bda12f6852ULL, 0x2f684bda12f684c4ULL, },
        { 0x555555555555555cULL, 0x555555555555555cULL, },
        { 0x0000000000000007ULL, 0x0000000000000007ULL, },    /*  24  */
        { 0x0000000000000007ULL, 0x0000000000000007ULL, },
        { 0x1c71c71c71c71c79ULL, 0x1c71c71c71c71c79ULL, },
        { 0xaaaaaaaaaaaaaab2ULL, 0xaaaaaaaaaaaaaab2ULL, },
        { 0x666666666666666eULL, 0x666666666666666eULL, },
        { 0x555555555555555dULL, 0x555555555555555dULL, },
        { 0x5ed097b425ed0983ULL, 0xed097b425ed097bcULL, },
        { 0x0000000000000008ULL, 0x0000000000000008ULL, },
        { 0x333333333333333cULL, 0x333333333333333cULL, },    /*  32  */
        { 0x333333333333333cULL, 0x333333333333333cULL, },
        { 0xaaaaaaaaaaaaaab4ULL, 0xaaaaaaaaaaaaaab4ULL, },
        { 0x6666666666666670ULL, 0x6666666666666670ULL, },
        { 0x5c28f5c28f5c2900ULL, 0x5c28f5c28f5c2900ULL, },
        { 0x99999999999999a4ULL, 0x99999999999999a4ULL, },
        { 0x16c16c16c16c16ccULL, 0xd27d27d27d27d288ULL, },
        { 0xccccccccccccccd8ULL, 0xccccccccccccccd8ULL, },
        { 0x99999999999999a5ULL, 0x99999999999999a5ULL, },    /*  40  */
        { 0x99999999999999a5ULL, 0x99999999999999a5ULL, },
        { 0x7777777777777783ULL, 0x7777777777777783ULL, },
        { 0x6666666666666672ULL, 0x6666666666666672ULL, },
        { 0xa3d70a3d70a3d716ULL, 0xa3d70a3d70a3d716ULL, },
        { 0x333333333333333fULL, 0x333333333333333fULL, },
        { 0xd27d27d27d27d289ULL, 0xc16c16c16c16c178ULL, },
        { 0x000000000000000cULL, 0x000000000000000cULL, },
        { 0x1c71c71c71c71c7eULL, 0xc71c71c71c71c729ULL, },    /*  48  */
        { 0x1c71c71c71c71c7eULL, 0xc71c71c71c71c729ULL, },
        { 0x2f684bda12f684caULL, 0xf684bda12f684be7ULL, },
        { 0x38e38e38e38e38f0ULL, 0x8e38e38e38e38e46ULL, },
        { 0xb60b60b60b60b618ULL, 0xc71c71c71c71c72aULL, },
        { 0x5555555555555562ULL, 0x5555555555555563ULL, },
        { 0x06522c3f35ba7826ULL, 0xa781948b0fcd6eacULL, },
        { 0x71c71c71c71c71d4ULL, 0x1c71c71c71c71c80ULL, },
        { 0x5555555555555563ULL, 0x5555555555555564ULL, },    /*  56  */
        { 0x5555555555555563ULL, 0x5555555555555564ULL, },
        { 0x97b425ed097b426dULL, 0x7b425ed097b425fcULL, },
        { 0x38e38e38e38e38f2ULL, 0x8e38e38e38e38e48ULL, },
        { 0xeeeeeeeeeeeeeefeULL, 0x8888888888888898ULL, },
        { 0x1c71c71c71c71c81ULL, 0xc71c71c71c71c72cULL, },
        { 0x87e6b74f0329162fULL, 0x3c0ca4587e6b7500ULL, },
        { 0x0000000000000010ULL, 0x0000000000000010ULL, },
        { 0xad45be6961639010ULL, 0x3297fdea749880a0ULL, },    /*  64  */
        { 0x9ced640a487afa10ULL, 0xeaa90809e3b1a470ULL, },
        { 0xa5b377aa0caf5a10ULL, 0x95c9a7903bd12160ULL, },
        { 0xa194ffe4fb27d390ULL, 0x17e6ccd3c9a1c0e0ULL, },
        { 0x913ca585e23f3d90ULL, 0xcff7d6f338bae4b0ULL, },
        { 0xc8ead0bee02cadd0ULL, 0x381c4d6a83a94cc0ULL, },
        { 0x33b60e279e9989d0ULL, 0xe7f71f9b97ee3470ULL, },
        { 0x217580abbfdd3e40ULL, 0x6779436687bc89f0ULL, },
        { 0x2a3b944b84119e40ULL, 0x1299e2ecdfdc06e0ULL, },    /*  72  */
        { 0x9506d1b4427e7a40ULL, 0xc274b51df420ee90ULL, },
        { 0x1b2bb7962782ba40ULL, 0x9bf62dc42637b820ULL, },
        { 0x91d16316b1663b40ULL, 0x3cf7c824fb128ca0ULL, },
        { 0x8db2eb519fdeb4c0ULL, 0xbf14ed6888e32c20ULL, },
        { 0x7b725dd5c1226930ULL, 0x3e97113378b181a0ULL, },
        { 0xf21809564b05ea30ULL, 0xdf98ab944d8c5620ULL, },
        { 0x3dcc402bfcefb9f4ULL, 0xf26a7a4530ab3a20ULL, },
        { 0x81a8956a21043af4ULL, 0xe63ec4a9de07f3a0ULL, },    /*  80  */
        { 0x14acc7eab115be94ULL, 0xa72fae300e450520ULL, },
        { 0x4c5c3900181b6494ULL, 0xc26796e561c70ba0ULL, },
        { 0x513451003792b1acULL, 0x5acad191d5b18fa0ULL, },
        { 0x0daff27cb51538acULL, 0x31375ce2aea24b20ULL, },
        { 0xbb9ebee52390b20cULL, 0xd8cfb350af547ea0ULL, },
        { 0x4df25269204a3c0cULL, 0x07b9241bbd1b8320ULL, },
        { 0x39b3c4d066371fb4ULL, 0x2a4dc00c264fb720ULL, },
        { 0xf9aee458846dd0b4ULL, 0x79d838b37c524ca0ULL, },    /*  88  */
        { 0x115f9e7f00744254ULL, 0x46ec87fe3540fa20ULL, },
        { 0xb01458f6b0850854ULL, 0xde82246a25db24a0ULL, },
        { 0xc18097bf5a7bb9ecULL, 0x4155f0da566748a0ULL, },
        { 0x70c7391b1a7d90ecULL, 0x0400deec0a0cb020ULL, },
        { 0xf7a41980bd958c4cULL, 0xedfeb14ff6d44fa0ULL, },
        { 0x7906f19718fcf64cULL, 0x29e471752ecca820ULL, },
        { 0xb6393967140b1974ULL, 0xbd0ed4c39361fc20ULL, },
        { 0x74ecb57da4acfa74ULL, 0x36ea3f3dbcafcda0ULL, },    /*  96  */
        { 0x5b14aa5e3f7c1b74ULL, 0xeb031f17fe2b7120ULL, },
        { 0x0468573ef6087c74ULL, 0xe8ef35d2e05abea0ULL, },
        { 0xd69cf5cf0de21d74ULL, 0x39f569701e89ae20ULL, },
        { 0xf233f7a10f743514ULL, 0xf574fc00c1b755a0ULL, },
        { 0x873c421a5ed469b4ULL, 0x96f393305dfcdf20ULL, },
        { 0x17e80b0449fea354ULL, 0x2f05ddb06b40c2a0ULL, },
        { 0x0741f67f982609f4ULL, 0x9c23f2dbc2b79820ULL, },
        { 0x530275e3b2de7ff4ULL, 0xc6904e7f6f6c1aa0ULL, },    /* 104  */
        { 0xf8214644bbe3f5f4ULL, 0xe44a0de01c974f20ULL, },
        { 0xb59c90c0a8b66bf4ULL, 0x9abcf7a8e1391da0ULL, },
        { 0xb67d543caed5e1f4ULL, 0x4ce8f72994d78e20ULL, },
        { 0xcee67f5e9d2e224cULL, 0xba31bdf2ab48a220ULL, },
        { 0x87acb43db40fad74ULL, 0x8a259794c40e3620ULL, },
        { 0x45c27495332aeeccULL, 0xe81c4208ecf84a20ULL, },
        { 0x50a99b794e1bc8f4ULL, 0x17cdf4c275d6de20ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADDV_D(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADDV_D(b128_random[i], b128_random[j],
                           b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                        (PATTERN_INPUTS_SHORT_COUNT)) +
                                       RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADDV_D__DDT(b128_random[i], b128_random[j],
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
            do_msa_MADDV_D__DSD(b128_random[i], b128_random[j],
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
