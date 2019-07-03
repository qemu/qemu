/*
 *  Test program for MSA instruction MSUBV.H
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
    char *instruction_name =  "MSUBV.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xaaa9aaa9aaa9aaa9ULL, 0xaaa9aaa9aaa9aaa9ULL, },
        { 0xfffefffefffefffeULL, 0xfffefffefffefffeULL, },
        { 0xcccacccacccacccaULL, 0xcccacccacccacccaULL, },
        { 0xfffdfffdfffdfffdULL, 0xfffdfffdfffdfffdULL, },
        { 0xe38b38e08e35e38bULL, 0x38e08e35e38b38e0ULL, },
        { 0xfffcfffcfffcfffcULL, 0xfffcfffcfffcfffcULL, },
        { 0xfffcfffcfffcfffcULL, 0xfffcfffcfffcfffcULL, },    /*   8  */
        { 0xfffcfffcfffcfffcULL, 0xfffcfffcfffcfffcULL, },
        { 0xfffcfffcfffcfffcULL, 0xfffcfffcfffcfffcULL, },
        { 0xfffcfffcfffcfffcULL, 0xfffcfffcfffcfffcULL, },
        { 0xfffcfffcfffcfffcULL, 0xfffcfffcfffcfffcULL, },
        { 0xfffcfffcfffcfffcULL, 0xfffcfffcfffcfffcULL, },
        { 0xfffcfffcfffcfffcULL, 0xfffcfffcfffcfffcULL, },
        { 0xfffcfffcfffcfffcULL, 0xfffcfffcfffcfffcULL, },
        { 0xaaa6aaa6aaa6aaa6ULL, 0xaaa6aaa6aaa6aaa6ULL, },    /*  16  */
        { 0xaaa6aaa6aaa6aaa6ULL, 0xaaa6aaa6aaa6aaa6ULL, },
        { 0x71c271c271c271c2ULL, 0x71c271c271c271c2ULL, },
        { 0x5550555055505550ULL, 0x5550555055505550ULL, },
        { 0xddd8ddd8ddd8ddd8ULL, 0xddd8ddd8ddd8ddd8ULL, },
        { 0xfffafffafffafffaULL, 0xfffafffafffafffaULL, },
        { 0x97ae7b3c5eca97aeULL, 0x7b3c5eca97ae7b3cULL, },
        { 0xaaa4aaa4aaa4aaa4ULL, 0xaaa4aaa4aaa4aaa4ULL, },
        { 0xfff9fff9fff9fff9ULL, 0xfff9fff9fff9fff9ULL, },    /*  24  */
        { 0xfff9fff9fff9fff9ULL, 0xfff9fff9fff9fff9ULL, },
        { 0xe387e387e387e387ULL, 0xe387e387e387e387ULL, },
        { 0x554e554e554e554eULL, 0x554e554e554e554eULL, },
        { 0x9992999299929992ULL, 0x9992999299929992ULL, },
        { 0xaaa3aaa3aaa3aaa3ULL, 0xaaa3aaa3aaa3aaa3ULL, },
        { 0xf67d6844da0bf67dULL, 0x6844da0bf67d6844ULL, },
        { 0xfff8fff8fff8fff8ULL, 0xfff8fff8fff8fff8ULL, },
        { 0xccc4ccc4ccc4ccc4ULL, 0xccc4ccc4ccc4ccc4ULL, },    /*  32  */
        { 0xccc4ccc4ccc4ccc4ULL, 0xccc4ccc4ccc4ccc4ULL, },
        { 0x554c554c554c554cULL, 0x554c554c554c554cULL, },
        { 0x9990999099909990ULL, 0x9990999099909990ULL, },
        { 0xd700d700d700d700ULL, 0xd700d700d700d700ULL, },
        { 0x665c665c665c665cULL, 0x665c665c665c665cULL, },
        { 0xe9342d7871bce934ULL, 0x2d7871bce9342d78ULL, },
        { 0x3328332833283328ULL, 0x3328332833283328ULL, },
        { 0x665b665b665b665bULL, 0x665b665b665b665bULL, },    /*  40  */
        { 0x665b665b665b665bULL, 0x665b665b665b665bULL, },
        { 0x887d887d887d887dULL, 0x887d887d887d887dULL, },
        { 0x998e998e998e998eULL, 0x998e998e998e998eULL, },
        { 0x28ea28ea28ea28eaULL, 0x28ea28ea28ea28eaULL, },
        { 0xccc1ccc1ccc1ccc1ULL, 0xccc1ccc1ccc1ccc1ULL, },
        { 0x2d773e884f992d77ULL, 0x3e884f992d773e88ULL, },
        { 0xfff4fff4fff4fff4ULL, 0xfff4fff4fff4fff4ULL, },
        { 0xe38238d78e2ce382ULL, 0x38d78e2ce38238d7ULL, },    /*  48  */
        { 0xe38238d78e2ce382ULL, 0x38d78e2ce38238d7ULL, },
        { 0x7b36b419ecfc7b36ULL, 0xb419ecfc7b36b419ULL, },
        { 0xc71071ba1c64c710ULL, 0x71ba1c64c71071baULL, },
        { 0x49e838d627c449e8ULL, 0x38d627c449e838d6ULL, },
        { 0xaa9eaa9daa9caa9eULL, 0xaa9daa9caa9eaa9dULL, },
        { 0x87da91547e5c87daULL, 0x91547e5c87da9154ULL, },
        { 0x8e2ce38038d48e2cULL, 0xe38038d48e2ce380ULL, },
        { 0xaa9daa9caa9baa9dULL, 0xaa9caa9baa9daa9cULL, },    /*  56  */
        { 0xaa9daa9caa9baa9dULL, 0xaa9caa9baa9daa9cULL, },
        { 0xbd93da04f675bd93ULL, 0xda04f675bd93da04ULL, },
        { 0xc70e71b81c62c70eULL, 0x71b81c62c70e71b8ULL, },
        { 0x11027768ddce1102ULL, 0x7768ddce11027768ULL, },
        { 0xe37f38d48e29e37fULL, 0x38d48e29e37f38d4ULL, },
        { 0xe9d18b0048a1e9d1ULL, 0x8b0048a1e9d18b00ULL, },
        { 0xfff0fff0fff0fff0ULL, 0xfff0fff0fff0fff0ULL, },
        { 0x340ccd603a6c6ff0ULL, 0x7c7fc96cb0d77f60ULL, },    /*  64  */
        { 0x07608c7c902605f0ULL, 0x7e1ef7e0f9925b90ULL, },
        { 0xda1ca10416e8a5f0ULL, 0x2e36f13e11e9dea0ULL, },
        { 0x6166ada860262c70ULL, 0x773f69ee43333f20ULL, },
        { 0x34ba6cc4b5e0c270ULL, 0x78de98628bee1b50ULL, },
        { 0x13b6467bf3775230ULL, 0xce8d99be266db340ULL, },
        { 0xeaeababdfe9a7630ULL, 0x2d251ed87fd8cb90ULL, },
        { 0x1b481af62b77c1c0ULL, 0x479e70e86e9a7610ULL, },
        { 0xee042f7eb23961c0ULL, 0xf7b66a4686f1f920ULL, },    /*  72  */
        { 0xc538a3c0bd5c85c0ULL, 0x564eef60e05c1170ULL, },
        { 0xb5941adce7fb45c0ULL, 0xd00e7d5f672347e0ULL, },
        { 0x25cef5ba555cc4c0ULL, 0x55b61e37e30d7360ULL, },
        { 0xad18025e9e9a4b40ULL, 0x9ebf96e71457d3e0ULL, },
        { 0xdd766297cb7796d0ULL, 0xb938e8f703197e60ULL, },
        { 0x4db03d7538d815d0ULL, 0x3ee089cf7f03a9e0ULL, },
        { 0x154fea4c3377460cULL, 0xe1ff538f49ffc5e0ULL, },
        { 0x4a99edbce7e9c70cULL, 0x3f66800dba7a7f60ULL, },    /*  80  */
        { 0xea0bfe08a81e3aacULL, 0xe7fcffbbd4745ce0ULL, },
        { 0x3e2ddcb809dc80acULL, 0xc75ca276a8f8bb60ULL, },
        { 0x5e4aa9605ec07444ULL, 0x6dc0dee66108df60ULL, },
        { 0x03a670e01940cf44ULL, 0x05802472d23066e0ULL, },
        { 0x8c72ca4059807924ULL, 0xb7002ade28606260ULL, },
        { 0x945efbc07b005b24ULL, 0x4f00c3bc4040d2e0ULL, },
        { 0xab5cc300f000ce2cULL, 0xf000bd1c6fc046e0ULL, },
        { 0xd7445f001000a72cULL, 0x600018d43e80f460ULL, },    /*  88  */
        { 0x66cca200e00039ccULL, 0xc000b74c5d00a5e0ULL, },
        { 0x33140e00c0008fccULL, 0xc0005a98be005060ULL, },
        { 0xafe8d8000000a7a4ULL, 0x00002a58c2005460ULL, },
        { 0x99d8b80000004aa4ULL, 0x0000d6088c005fe0ULL, },
        { 0xa388900000007984ULL, 0x0000413818003f60ULL, },
        { 0xc5b8f00000000b84ULL, 0x0000fa7010006be0ULL, },
        { 0x41f0c0000000014cULL, 0x00002bf0f0003fe0ULL, },
        { 0x7490c0000000724cULL, 0x0000b9d0a0004160ULL, },    /*  96  */
        { 0xb0f0c0000000a34cULL, 0x00008f70c00030e0ULL, },
        { 0xed90c0000000944cULL, 0x000014508000e660ULL, },
        { 0x0ff0c0000000454cULL, 0x00002ef0000019e0ULL, },
        { 0xebd08000000006ecULL, 0x00001a900000e160ULL, },
        { 0xf770000000005b8cULL, 0x000037f0000046e0ULL, },
        { 0x825000000000ab2cULL, 0x000039900000c260ULL, },
        { 0x5af0000000001dccULL, 0x000030f00000abe0ULL, },
        { 0x22900000000073ccULL, 0x0000d1e00000de60ULL, },    /* 104  */
        { 0x3bf000000000c9ccULL, 0x000083c000009ee0ULL, },
        { 0xe990000000001fccULL, 0x0000c7800000d560ULL, },
        { 0x0cf00000000075ccULL, 0x00000f00000049e0ULL, },
        { 0x0ee00000000079a4ULL, 0x0000670000005de0ULL, },
        { 0x77c000000000a1acULL, 0x00007f000000f1e0ULL, },
        { 0x8380000000008744ULL, 0x00005700000005e0ULL, },
        { 0xef0000000000488cULL, 0x0000ef00000099e0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MSUBV_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MSUBV_H(b128_random[i], b128_random[j],
                           b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                        (PATTERN_INPUTS_SHORT_COUNT)) +
                                       RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MSUBV_H__DDT(b128_random[i], b128_random[j],
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
            do_msa_MSUBV_H__DSD(b128_random[i], b128_random[j],
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
