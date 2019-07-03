/*
 *  Test program for MSA instruction MSUB_Q.W
 *
 *  Copyright (C) 2019  Wave Computing, Inc.
 *  Copyright (C) 2019  Aleksandar Markovic <amarkovic@wavecomp.com>
 *  Copyright (C) 2019  RT-RK Computer Based Systems LLC
 *  Copyright (C) 2019  Mateja Marjanovic <mateja.marjanovic@rt-rk.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
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
    char *group_name = "Fixed Multiply";
    char *instruction_name =  "MSUB_Q.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xfffffffefffffffeULL, 0xfffffffefffffffeULL, },
        { 0xfffffffefffffffeULL, 0xfffffffefffffffeULL, },
        { 0xfffffffdfffffffdULL, 0xfffffffdfffffffdULL, },
        { 0xfffffffdfffffffdULL, 0xfffffffdfffffffdULL, },
        { 0xfffffffcfffffffcULL, 0xfffffffdfffffffcULL, },
        { 0xfffffffcfffffffcULL, 0xfffffffcfffffffcULL, },
        { 0xfffffffcfffffffcULL, 0xfffffffcfffffffcULL, },    /*   8  */
        { 0xfffffffcfffffffcULL, 0xfffffffcfffffffcULL, },
        { 0xfffffffcfffffffcULL, 0xfffffffcfffffffcULL, },
        { 0xfffffffcfffffffcULL, 0xfffffffcfffffffcULL, },
        { 0xfffffffcfffffffcULL, 0xfffffffcfffffffcULL, },
        { 0xfffffffcfffffffcULL, 0xfffffffcfffffffcULL, },
        { 0xfffffffcfffffffcULL, 0xfffffffcfffffffcULL, },
        { 0xfffffffcfffffffcULL, 0xfffffffcfffffffcULL, },
        { 0xfffffffbfffffffbULL, 0xfffffffbfffffffbULL, },    /*  16  */
        { 0xfffffffbfffffffbULL, 0xfffffffbfffffffbULL, },
        { 0xc71c71c1c71c71c1ULL, 0xc71c71c1c71c71c1ULL, },
        { 0xfffffffafffffffaULL, 0xfffffffafffffffaULL, },
        { 0xddddddd7ddddddd7ULL, 0xddddddd7ddddddd7ULL, },
        { 0xfffffff9fffffff9ULL, 0xfffffff9fffffff9ULL, },
        { 0xed097b3ab425ed01ULL, 0x25ed0973ed097b3aULL, },
        { 0xfffffff7fffffff7ULL, 0xfffffff7fffffff7ULL, },
        { 0xfffffff7fffffff7ULL, 0xfffffff7fffffff7ULL, },    /*  24  */
        { 0xfffffff7fffffff7ULL, 0xfffffff7fffffff7ULL, },
        { 0x38e38e3038e38e30ULL, 0x38e38e3038e38e30ULL, },
        { 0xfffffff7fffffff7ULL, 0xfffffff7fffffff7ULL, },
        { 0x2222221922222219ULL, 0x2222221922222219ULL, },
        { 0xfffffff7fffffff7ULL, 0xfffffff7fffffff7ULL, },
        { 0x12f684b44bda12edULL, 0xda12f67c12f684b4ULL, },
        { 0xfffffff6fffffff7ULL, 0xfffffff7fffffff6ULL, },
        { 0xfffffff5fffffff6ULL, 0xfffffff6fffffff5ULL, },    /*  32  */
        { 0xfffffff5fffffff6ULL, 0xfffffff6fffffff5ULL, },
        { 0xddddddd2ddddddd3ULL, 0xddddddd3ddddddd2ULL, },
        { 0xfffffff4fffffff5ULL, 0xfffffff5fffffff4ULL, },
        { 0xeb851eabeb851eacULL, 0xeb851eaceb851eabULL, },
        { 0xfffffff2fffffff3ULL, 0xfffffff3fffffff2ULL, },
        { 0xf49f49e6d27d27c4ULL, 0x16c16c09f49f49e6ULL, },
        { 0xfffffff1fffffff1ULL, 0xfffffff1fffffff1ULL, },
        { 0xfffffff1fffffff1ULL, 0xfffffff1fffffff1ULL, },    /*  40  */
        { 0xfffffff1fffffff1ULL, 0xfffffff1fffffff1ULL, },
        { 0x2222221322222213ULL, 0x2222221322222213ULL, },
        { 0xfffffff1fffffff1ULL, 0xfffffff1fffffff1ULL, },
        { 0x147ae138147ae138ULL, 0x147ae138147ae138ULL, },
        { 0xfffffff0fffffff0ULL, 0xfffffff0fffffff0ULL, },
        { 0x0b60b5fb2d82d81dULL, 0xe93e93d90b60b5fbULL, },
        { 0xffffffefffffffefULL, 0xffffffefffffffefULL, },
        { 0xffffffeeffffffeeULL, 0xffffffefffffffeeULL, },    /*  48  */
        { 0xffffffeeffffffeeULL, 0xffffffefffffffeeULL, },
        { 0xed097b2fb425ecf6ULL, 0x25ed0969ed097b2fULL, },
        { 0xffffffecffffffecULL, 0xffffffeeffffffecULL, },
        { 0xf49f49e0d27d27bdULL, 0x16c16c04f49f49e0ULL, },
        { 0xffffffebffffffeaULL, 0xffffffedffffffebULL, },
        { 0xf9add3ab9add3bf6ULL, 0xe6b74ef0f9add3abULL, },
        { 0xffffffeaffffffe8ULL, 0xffffffecffffffeaULL, },
        { 0xffffffeaffffffe8ULL, 0xffffffebffffffeaULL, },    /*  56  */
        { 0xffffffeaffffffe8ULL, 0xffffffebffffffeaULL, },
        { 0x12f684a74bda12deULL, 0xda12f66f12f684a7ULL, },
        { 0xffffffe9ffffffe8ULL, 0xffffffeaffffffe9ULL, },
        { 0x0b60b5f42d82d815ULL, 0xe93e93d20b60b5f4ULL, },
        { 0xffffffe8ffffffe7ULL, 0xffffffe8ffffffe8ULL, },
        { 0x06522c276522c3d9ULL, 0x1948b0e406522c27ULL, },
        { 0xffffffe7ffffffe7ULL, 0xffffffe7ffffffe7ULL, },
        { 0x9048175df3423f14ULL, 0xd394eba0fffb65e2ULL, },    /*  64  */
        { 0x8c4dc60edac87812ULL, 0xc8687ef6003bdb1bULL, },
        { 0x80000000f0ed8852ULL, 0xb0ef6662ff3a80e6ULL, },
        { 0xe8ec58e8d33594acULL, 0xf41fb1f8fe335d76ULL, },
        { 0xe4f20799babbcdaaULL, 0xe8f3454efe73d2afULL, },
        { 0xe4cdc5978bb75798ULL, 0xe623b939faecec20ULL, },
        { 0xe2057a0fb6418676ULL, 0xe03c1eae0901cfcdULL, },
        { 0xe5c1db3280000000ULL, 0xf122e6111767bfefULL, },
        { 0x979cbaab96251040ULL, 0xd9a9cd7d166665baULL, },    /*  72  */
        { 0x94d46f23c0af3f1eULL, 0xd3c232f2247b4967ULL, },
        { 0x800000009a322d5aULL, 0xc75aaa8dec42881aULL, },
        { 0xc96455c6cdd91d8cULL, 0xeadc3c95b2c62f40ULL, },
        { 0x3250aeaeb02129e6ULL, 0x2e0c882bb1bf0bd0ULL, },
        { 0x360d0fd180000000ULL, 0x3ef34f8ec024fbf2ULL, },
        { 0x7f716597b3a6f032ULL, 0x6274e19686a8a318ULL, },
        { 0x1ce6cdb280000000ULL, 0xfcd31bb480000000ULL, },
        { 0x37e70b49a8625540ULL, 0xfeb1f7e080000000ULL, },    /*  80  */
        { 0x39c31699dd7c5546ULL, 0xfee37780953f52fcULL, },
        { 0x5f82316fca8f431eULL, 0xff3c0af780000000ULL, },
        { 0x0bb5432ff1e2e177ULL, 0xfe8d6e9580000000ULL, },
        { 0x16a56af3f656d2b3ULL, 0xff67ba1b80000000ULL, },
        { 0x17664384fc31bf42ULL, 0xff7e4aa4953f52fcULL, },
        { 0x26b0cbfdfa1b830bULL, 0xffa6ab9180000000ULL, },
        { 0x04be31a4fe719ab1ULL, 0xff57124580000000ULL, },
        { 0x092c8a1ffeef4c68ULL, 0xffba958e80000000ULL, },    /*  88  */
        { 0x097aa960ff949347ULL, 0xffc4dede953f52fcULL, },
        { 0x0fac7158ff59ab27ULL, 0xffd7471a80000000ULL, },
        { 0x01ebdf01ffd41248ULL, 0xffb2fdd380000000ULL, },
        { 0x03b76546ffe1ee50ULL, 0xffe05b1780000000ULL, },
        { 0x03d70afdfff427aaULL, 0xffe50b86953f52fcULL, },
        { 0x065971c1ffeda8dfULL, 0xffed6fa980000000ULL, },
        { 0x00c741e8fffb2801ULL, 0xffdce50280000000ULL, },
        { 0x01816947fffcaf39ULL, 0xfff1931580000000ULL, },    /*  96  */
        { 0x02e97a17fffdbb03ULL, 0xfffa128380000000ULL, },
        { 0x05a1edf3fffe7250ULL, 0xfffd906f80000000ULL, },
        { 0x0ae508c5fffeefc8ULL, 0xfffeffc380000000ULL, },
        { 0x0b41cf1bffff94c3ULL, 0xffff25bb953f52fcULL, },
        { 0x0ba1ab79ffffd5c1ULL, 0xffff4613a6f7bf69ULL, },
        { 0x0c04b828ffffef5bULL, 0xffff61a0b5bf25caULL, },
        { 0x0c6b104efffff971ULL, 0xffff7918c21285a5ULL, },
        { 0x148886c7fffff5d8ULL, 0xffffa3179907b21bULL, },    /* 104  */
        { 0x21f39335fffff046ULL, 0xffffc00380000000ULL, },
        { 0x38235e38ffffe7a6ULL, 0xffffd3ee80000000ULL, },
        { 0x5cd2ce93ffffda4bULL, 0xffffe1a680000000ULL, },
        { 0x0b60ff8afffff60aULL, 0xffffc69a80000000ULL, },
        { 0x01651818fffffd5eULL, 0xffff937480000000ULL, },
        { 0x002bc65fffffff4dULL, 0xffff32bb80000000ULL, },
        { 0x00055dbfffffffd0ULL, 0xfffe7bd280000000ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MSUB_Q_W(b128_pattern[i], b128_pattern[j],
                            b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MSUB_Q_W(b128_random[i], b128_random[j],
                            b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                         (PATTERN_INPUTS_SHORT_COUNT)) +
                                        RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MSUB_Q_W__DDT(b128_random[i], b128_random[j],
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
            do_msa_MSUB_Q_W__DSD(b128_random[i], b128_random[j],
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
