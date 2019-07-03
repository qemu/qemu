/*
 *  Test program for MSA instruction MSUBR_Q.W
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
    char *instruction_name =  "MSUBR_Q.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000001ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },    /*  16  */
        { 0x0000000100000001ULL, 0x0000000100000001ULL, },
        { 0x38e38e3b38e38e3bULL, 0x38e38e3b38e38e3bULL, },
        { 0x0000000200000002ULL, 0x0000000200000002ULL, },
        { 0x2222222522222225ULL, 0x2222222522222225ULL, },
        { 0x0000000300000003ULL, 0x0000000300000003ULL, },
        { 0x12f684c14bda12faULL, 0xda12f68812f684c1ULL, },
        { 0x0000000400000003ULL, 0x0000000400000004ULL, },
        { 0x0000000300000002ULL, 0x0000000300000003ULL, },    /*  24  */
        { 0x0000000300000002ULL, 0x0000000300000003ULL, },
        { 0xc71c71cac71c71c9ULL, 0xc71c71cac71c71caULL, },
        { 0x0000000200000001ULL, 0x0000000200000002ULL, },
        { 0xdddddddfdddddddeULL, 0xdddddddfdddddddfULL, },
        { 0x0000000100000000ULL, 0x0000000100000001ULL, },
        { 0xed097b43b425ed0aULL, 0x25ed097ced097b43ULL, },
        { 0x0000000000000000ULL, 0x0000000100000000ULL, },
        { 0x0000000000000000ULL, 0x0000000100000000ULL, },    /*  32  */
        { 0x0000000000000000ULL, 0x0000000100000000ULL, },
        { 0x2222222322222223ULL, 0x2222222422222223ULL, },
        { 0x0000000000000000ULL, 0x0000000100000000ULL, },
        { 0x147ae148147ae148ULL, 0x147ae149147ae148ULL, },
        { 0x0000000000000000ULL, 0x0000000100000000ULL, },
        { 0x0b60b60c2d82d82eULL, 0xe93e93ea0b60b60cULL, },
        { 0x0000000100000000ULL, 0x0000000100000001ULL, },
        { 0x0000000100000000ULL, 0x0000000100000001ULL, },    /*  40  */
        { 0x0000000100000000ULL, 0x0000000100000001ULL, },
        { 0xdddddddfdddddddeULL, 0xdddddddfdddddddfULL, },
        { 0x0000000100000000ULL, 0x0000000100000001ULL, },
        { 0xeb851eb9eb851eb8ULL, 0xeb851eb9eb851eb9ULL, },
        { 0x0000000100000000ULL, 0x0000000100000001ULL, },
        { 0xf49f49f5d27d27d3ULL, 0x16c16c17f49f49f5ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000001ULL, 0x0000000000000000ULL, },    /*  48  */
        { 0x0000000000000001ULL, 0x0000000000000000ULL, },
        { 0x12f684be4bda12f8ULL, 0xda12f68512f684beULL, },
        { 0x0000000000000002ULL, 0x0000000000000000ULL, },
        { 0x0b60b60c2d82d830ULL, 0xe93e93e90b60b60cULL, },
        { 0x0000000000000003ULL, 0xffffffff00000000ULL, },
        { 0x06522c3f6522c3f7ULL, 0x1948b0fb06522c3fULL, },
        { 0x0000000000000004ULL, 0xffffffff00000000ULL, },
        { 0x0000000000000003ULL, 0xffffffff00000000ULL, },    /*  56  */
        { 0x0000000000000003ULL, 0xffffffff00000000ULL, },
        { 0xed097b43b425ed0cULL, 0x25ed097bed097b43ULL, },
        { 0x0000000000000002ULL, 0x0000000000000000ULL, },
        { 0xf49f49f5d27d27d4ULL, 0x16c16c17f49f49f5ULL, },
        { 0x0000000000000001ULL, 0x0000000000000000ULL, },
        { 0xf9add3c19add3c0eULL, 0xe6b74f04f9add3c1ULL, },
        { 0x0000000000000000ULL, 0x0000000100000000ULL, },
        { 0x6fb7e8890cbdc0d3ULL, 0x2c6b144700049a05ULL, },    /*  64  */
        { 0x73b239d7253787d5ULL, 0x379780f0ffc424ccULL, },
        { 0x7fffffff0f127795ULL, 0x4f10998300c57f01ULL, },
        { 0x1713a7162cca6b3bULL, 0x0be04ded01cca270ULL, },
        { 0x1b0df8644544323dULL, 0x170cba96018c2d37ULL, },
        { 0x1b323a657448a84fULL, 0x19dc46aa051313c6ULL, },
        { 0x1dfa85ed49be7970ULL, 0x1fc3e135f6fe3018ULL, },
        { 0x1a3e24ca7fffffffULL, 0x0edd19d1e8983ff6ULL, },
        { 0x6863455169daefbfULL, 0x26563264e9999a2bULL, },    /*  72  */
        { 0x6b2b90d93f50c0e0ULL, 0x2c3dccefdb84b67dULL, },
        { 0x7fffffff65cdd2a4ULL, 0x38a5555313bd77c9ULL, },
        { 0x369baa393226e271ULL, 0x1523c34a4d39d0a3ULL, },
        { 0xcdaf51504fded617ULL, 0xd1f377b44e40f412ULL, },
        { 0xc9f2f02d7fffffffULL, 0xc10cb0503fdb03f0ULL, },
        { 0x808e9a674c590fccULL, 0x9d8b1e4779575ccaULL, },
        { 0xe319324b7fffffffULL, 0x032ce4297fffffffULL, },
        { 0xfe196fe67fffffffULL, 0x050bc0417e7bb00bULL, },    /*  80  */
        { 0xfe299f487fffffffULL, 0x05cb2b577fffffffULL, },
        { 0xff5d018339cf8b80ULL, 0x0798e2662b2b2514ULL, },
        { 0xfecdfe20645a7d9bULL, 0x00d3dcf80dea608eULL, },
        { 0xffebe0517fffffffULL, 0x0150ab000dc02968ULL, },
        { 0xffec8baf7fffffffULL, 0x01828ea210087db2ULL, },
        { 0xfff9423c39cf8b80ULL, 0x01fae4c1056841b9ULL, },
        { 0xfff35806645a7d9bULL, 0x003737bc01be3862ULL, },
        { 0xffff2aee7fffffffULL, 0x0057bed801b8eeb0ULL, },    /*  88  */
        { 0xffff32047fffffffULL, 0x0064bf8102021ffcULL, },
        { 0xffffb89f39cf8b80ULL, 0x00841c7a00ad640aULL, },
        { 0xffff79fe645a7d9bULL, 0x000e642f0037e4a6ULL, },
        { 0xfffff7307fffffffULL, 0x0016de7800373b16ULL, },
        { 0xfffff77b7fffffffULL, 0x001a42040040661aULL, },
        { 0xfffffd0c39cf8b80ULL, 0x00226e990015b802ULL, },
        { 0xfffffa75645a7d9bULL, 0x0003c0350007004aULL, },
        { 0xffffffa37fffffffULL, 0x0005f5d90006eb0dULL, },    /*  96  */
        { 0xfffffffa7fffffffULL, 0x000978b30006d610ULL, },
        { 0x000000007fffffffULL, 0x000f0d0c0006c153ULL, },
        { 0x000000007fffffffULL, 0x0017eacf0006acd5ULL, },
        { 0x000000007fffffffULL, 0x001b761e0007c87dULL, },
        { 0x000000007fffffffULL, 0x001f87e00009133bULL, },
        { 0x000000007fffffffULL, 0x00243402000a94e0ULL, },
        { 0x000000007fffffffULL, 0x00299164000c5689ULL, },
        { 0x0000000039cf8b80ULL, 0x003682160004293bULL, },    /* 104  */
        { 0x000000001a1c28c4ULL, 0x00477a0900016741ULL, },
        { 0x000000000bcae026ULL, 0x005dba4500007929ULL, },
        { 0x00000000055376c2ULL, 0x007ae7c2000028ddULL, },
        { 0x00000000093ed557ULL, 0x000d637500000d2cULL, },
        { 0x00000000100c9469ULL, 0x0001755d0000043fULL, },
        { 0x000000001bdc1297ULL, 0x000028ac0000015eULL, },
        { 0x00000000305c8bbfULL, 0x0000046e00000071ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADDR_Q_W(b128_pattern[i], b128_pattern[j],
                             b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADDR_Q_W(b128_random[i], b128_random[j],
                             b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                          (PATTERN_INPUTS_SHORT_COUNT)) +
                                         RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADDR_Q_W__DDT(b128_random[i], b128_random[j],
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
            do_msa_MADDR_Q_W__DSD(b128_random[i], b128_random[j],
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
