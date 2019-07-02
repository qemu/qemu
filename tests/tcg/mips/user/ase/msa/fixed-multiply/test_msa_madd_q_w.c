/*
 *  Test program for MSA instruction MADD_Q.W
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
    char *instruction_name =  "MADD_Q.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xfffffffefffffffeULL, 0xfffffffefffffffeULL, },
        { 0xfffffffefffffffeULL, 0xfffffffdfffffffeULL, },
        { 0xfffffffdfffffffdULL, 0xfffffffdfffffffdULL, },
        { 0xfffffffdfffffffdULL, 0xfffffffdfffffffdULL, },    /*   8  */
        { 0xfffffffdfffffffdULL, 0xfffffffdfffffffdULL, },
        { 0xfffffffdfffffffdULL, 0xfffffffdfffffffdULL, },
        { 0xfffffffdfffffffdULL, 0xfffffffdfffffffdULL, },
        { 0xfffffffdfffffffdULL, 0xfffffffdfffffffdULL, },
        { 0xfffffffdfffffffdULL, 0xfffffffdfffffffdULL, },
        { 0xfffffffdfffffffdULL, 0xfffffffdfffffffdULL, },
        { 0xfffffffdfffffffdULL, 0xfffffffdfffffffdULL, },
        { 0xfffffffdfffffffdULL, 0xfffffffdfffffffdULL, },    /*  16  */
        { 0xfffffffdfffffffdULL, 0xfffffffdfffffffdULL, },
        { 0x38e38e3638e38e36ULL, 0x38e38e3638e38e36ULL, },
        { 0xfffffffcfffffffcULL, 0xfffffffcfffffffcULL, },
        { 0x2222221e2222221eULL, 0x2222221e2222221eULL, },
        { 0xfffffffbfffffffbULL, 0xfffffffbfffffffbULL, },
        { 0x12f684b94bda12f2ULL, 0xda12f68012f684b9ULL, },
        { 0xfffffffbfffffffbULL, 0xfffffffbfffffffbULL, },
        { 0xfffffffafffffffaULL, 0xfffffffafffffffaULL, },    /*  24  */
        { 0xfffffffafffffffaULL, 0xfffffffafffffffaULL, },
        { 0xc71c71c0c71c71c0ULL, 0xc71c71c0c71c71c0ULL, },
        { 0xfffffff8fffffff8ULL, 0xfffffff8fffffff8ULL, },
        { 0xddddddd5ddddddd5ULL, 0xddddddd5ddddddd5ULL, },
        { 0xfffffff6fffffff6ULL, 0xfffffff6fffffff6ULL, },
        { 0xed097b38b425ecffULL, 0x25ed0970ed097b38ULL, },
        { 0xfffffff5fffffff4ULL, 0xfffffff4fffffff5ULL, },
        { 0xfffffff5fffffff4ULL, 0xfffffff4fffffff5ULL, },    /*  32  */
        { 0xfffffff5fffffff4ULL, 0xfffffff4fffffff5ULL, },
        { 0x2222221722222216ULL, 0x2222221622222217ULL, },
        { 0xfffffff4fffffff3ULL, 0xfffffff3fffffff4ULL, },
        { 0x147ae13c147ae13bULL, 0x147ae13b147ae13cULL, },
        { 0xfffffff4fffffff3ULL, 0xfffffff3fffffff4ULL, },
        { 0x0b60b5ff2d82d821ULL, 0xe93e93dc0b60b5ffULL, },
        { 0xfffffff3fffffff3ULL, 0xfffffff3fffffff3ULL, },
        { 0xfffffff2fffffff2ULL, 0xfffffff2fffffff2ULL, },    /*  40  */
        { 0xfffffff2fffffff2ULL, 0xfffffff2fffffff2ULL, },
        { 0xddddddcfddddddcfULL, 0xddddddcfddddddcfULL, },
        { 0xfffffff0fffffff0ULL, 0xfffffff0fffffff0ULL, },
        { 0xeb851ea8eb851ea8ULL, 0xeb851ea8eb851ea8ULL, },
        { 0xffffffefffffffefULL, 0xffffffefffffffefULL, },
        { 0xf49f49e3d27d27c1ULL, 0x16c16c05f49f49e3ULL, },
        { 0xffffffeeffffffeeULL, 0xffffffeeffffffeeULL, },
        { 0xffffffeeffffffeeULL, 0xffffffedffffffeeULL, },    /*  48  */
        { 0xffffffeeffffffeeULL, 0xffffffedffffffeeULL, },
        { 0x12f684ac4bda12e5ULL, 0xda12f67212f684acULL, },
        { 0xffffffeeffffffeeULL, 0xffffffecffffffeeULL, },
        { 0x0b60b5f92d82d81cULL, 0xe93e93d50b60b5f9ULL, },
        { 0xffffffedffffffeeULL, 0xffffffebffffffedULL, },
        { 0x06522c2c6522c3e1ULL, 0x1948b0e706522c2cULL, },
        { 0xffffffecffffffeeULL, 0xffffffeaffffffecULL, },
        { 0xffffffebffffffedULL, 0xffffffeaffffffebULL, },    /*  56  */
        { 0xffffffebffffffedULL, 0xffffffeaffffffebULL, },
        { 0xed097b2db425ecf6ULL, 0x25ed0965ed097b2dULL, },
        { 0xffffffeaffffffebULL, 0xffffffe9ffffffeaULL, },
        { 0xf49f49ded27d27bdULL, 0x16c16c00f49f49deULL, },
        { 0xffffffe9ffffffeaULL, 0xffffffe9ffffffe9ULL, },
        { 0xf9add3a99add3bf7ULL, 0xe6b74eecf9add3a9ULL, },
        { 0xffffffe8ffffffe8ULL, 0xffffffe8ffffffe8ULL, },
        { 0x6fb7e8710cbdc0baULL, 0x2c6b142e000499ecULL, },    /*  64  */
        { 0x73b239bf253787bbULL, 0x379780d7ffc424b2ULL, },
        { 0x7fffffff0f12777aULL, 0x4f10996a00c57ee6ULL, },
        { 0x1713a7162cca6b1fULL, 0x0be04dd301cca255ULL, },
        { 0x1b0df86445443220ULL, 0x170cba7c018c2d1bULL, },
        { 0x1b323a657448a831ULL, 0x19dc4690051313a9ULL, },
        { 0x1dfa85ec49be7952ULL, 0x1fc3e11af6fe2ffbULL, },
        { 0x1a3e24c87fffffffULL, 0x0edd19b6e8983fd8ULL, },
        { 0x6863454e69daefbeULL, 0x26563249e9999a0cULL, },    /*  72  */
        { 0x6b2b90d53f50c0dfULL, 0x2c3dccd3db84b65eULL, },
        { 0x7fffffff65cdd2a2ULL, 0x38a5553713bd77aaULL, },
        { 0x369baa383226e26fULL, 0x1523c32e4d39d083ULL, },
        { 0xcdaf514f4fded614ULL, 0xd1f377974e40f3f2ULL, },
        { 0xc9f2f02b7fffffffULL, 0xc10cb0333fdb03cfULL, },
        { 0x808e9a644c590fccULL, 0x9d8b1e2a79575ca8ULL, },
        { 0xe31932487fffffffULL, 0x032ce40b7fffffffULL, },
        { 0xfe196fe57fffffffULL, 0x050bc0117e7bb00bULL, },    /*  80  */
        { 0xfe299f467fffffffULL, 0x05cb2b207fffffffULL, },
        { 0xff5d018239cf8b7fULL, 0x0798e21e2b2b2513ULL, },
        { 0xfecdfe1e645a7d99ULL, 0x00d3dcf00dea608dULL, },
        { 0xffebe0507fffffffULL, 0x0150aaf30dc02967ULL, },
        { 0xffec8bad7fffffffULL, 0x01828e9310087db0ULL, },
        { 0xfff9423b39cf8b7fULL, 0x01fae4ad056841b8ULL, },
        { 0xfff35804645a7d99ULL, 0x003737ba01be3861ULL, },
        { 0xffff2aee7fffffffULL, 0x0057bed401b8eeafULL, },    /*  88  */
        { 0xffff32047fffffffULL, 0x0064bf7c02021ffbULL, },
        { 0xffffb89f39cf8b7fULL, 0x00841c7300ad6409ULL, },
        { 0xffff79fe645a7d99ULL, 0x000e642e0037e4a5ULL, },
        { 0xfffff72f7fffffffULL, 0x0016de7600373b15ULL, },
        { 0xfffff77a7fffffffULL, 0x001a420100406619ULL, },
        { 0xfffffd0b39cf8b7fULL, 0x00226e950015b801ULL, },
        { 0xfffffa72645a7d99ULL, 0x0003c03400070049ULL, },
        { 0xffffffa27fffffffULL, 0x0005f5d70006eb0bULL, },    /*  96  */
        { 0xfffffff97fffffffULL, 0x000978af0006d60eULL, },
        { 0xffffffff7fffffffULL, 0x000f0d050006c150ULL, },
        { 0xffffffff7fffffffULL, 0x0017eac30006acd1ULL, },
        { 0xffffffff7fffffffULL, 0x001b76100007c878ULL, },
        { 0xffffffff7fffffffULL, 0x001f87d000091335ULL, },
        { 0xffffffff7fffffffULL, 0x002433ef000a94d9ULL, },
        { 0xffffffff7fffffffULL, 0x0029914d000c5680ULL, },
        { 0xffffffff39cf8b7fULL, 0x003681f800042937ULL, },    /* 104  */
        { 0xffffffff1a1c28c3ULL, 0x004779e10001673fULL, },
        { 0xffffffff0bcae025ULL, 0x005dba1000007928ULL, },
        { 0xffffffff055376c1ULL, 0x007ae77c000028dcULL, },
        { 0xfffffffe093ed554ULL, 0x000d636d00000d2bULL, },
        { 0xfffffffc100c9463ULL, 0x0001755c0000043eULL, },
        { 0xfffffff81bdc128cULL, 0x000028ab0000015eULL, },
        { 0xfffffff0305c8babULL, 0x0000046e00000070ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADD_Q_W(b128_pattern[i], b128_pattern[j],
                            b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADD_Q_W(b128_random[i], b128_random[j],
                            b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                         (PATTERN_INPUTS_SHORT_COUNT)) +
                                        RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADD_Q_W__DDT(b128_random[i], b128_random[j],
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
            do_msa_MADD_Q_W__DSD(b128_random[i], b128_random[j],
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
