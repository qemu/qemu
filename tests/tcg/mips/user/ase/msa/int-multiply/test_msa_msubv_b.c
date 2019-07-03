/*
 *  Test program for MSA instruction MSUBV.B
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
    char *instruction_name =  "MSUBV.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xa9a9a9a9a9a9a9a9ULL, 0xa9a9a9a9a9a9a9a9ULL, },
        { 0xfefefefefefefefeULL, 0xfefefefefefefefeULL, },
        { 0xcacacacacacacacaULL, 0xcacacacacacacacaULL, },
        { 0xfdfdfdfdfdfdfdfdULL, 0xfdfdfdfdfdfdfdfdULL, },
        { 0xe08b35e08b35e08bULL, 0x35e08b35e08b35e0ULL, },
        { 0xfcfcfcfcfcfcfcfcULL, 0xfcfcfcfcfcfcfcfcULL, },
        { 0xfcfcfcfcfcfcfcfcULL, 0xfcfcfcfcfcfcfcfcULL, },    /*   8  */
        { 0xfcfcfcfcfcfcfcfcULL, 0xfcfcfcfcfcfcfcfcULL, },
        { 0xfcfcfcfcfcfcfcfcULL, 0xfcfcfcfcfcfcfcfcULL, },
        { 0xfcfcfcfcfcfcfcfcULL, 0xfcfcfcfcfcfcfcfcULL, },
        { 0xfcfcfcfcfcfcfcfcULL, 0xfcfcfcfcfcfcfcfcULL, },
        { 0xfcfcfcfcfcfcfcfcULL, 0xfcfcfcfcfcfcfcfcULL, },
        { 0xfcfcfcfcfcfcfcfcULL, 0xfcfcfcfcfcfcfcfcULL, },
        { 0xfcfcfcfcfcfcfcfcULL, 0xfcfcfcfcfcfcfcfcULL, },
        { 0xa6a6a6a6a6a6a6a6ULL, 0xa6a6a6a6a6a6a6a6ULL, },    /*  16  */
        { 0xa6a6a6a6a6a6a6a6ULL, 0xa6a6a6a6a6a6a6a6ULL, },
        { 0xc2c2c2c2c2c2c2c2ULL, 0xc2c2c2c2c2c2c2c2ULL, },
        { 0x5050505050505050ULL, 0x5050505050505050ULL, },
        { 0xd8d8d8d8d8d8d8d8ULL, 0xd8d8d8d8d8d8d8d8ULL, },
        { 0xfafafafafafafafaULL, 0xfafafafafafafafaULL, },
        { 0x3caeca3caeca3caeULL, 0xca3caeca3caeca3cULL, },
        { 0xa4a4a4a4a4a4a4a4ULL, 0xa4a4a4a4a4a4a4a4ULL, },
        { 0xf9f9f9f9f9f9f9f9ULL, 0xf9f9f9f9f9f9f9f9ULL, },    /*  24  */
        { 0xf9f9f9f9f9f9f9f9ULL, 0xf9f9f9f9f9f9f9f9ULL, },
        { 0x8787878787878787ULL, 0x8787878787878787ULL, },
        { 0x4e4e4e4e4e4e4e4eULL, 0x4e4e4e4e4e4e4e4eULL, },
        { 0x9292929292929292ULL, 0x9292929292929292ULL, },
        { 0xa3a3a3a3a3a3a3a3ULL, 0xa3a3a3a3a3a3a3a3ULL, },
        { 0x447d0b447d0b447dULL, 0x0b447d0b447d0b44ULL, },
        { 0xf8f8f8f8f8f8f8f8ULL, 0xf8f8f8f8f8f8f8f8ULL, },
        { 0xc4c4c4c4c4c4c4c4ULL, 0xc4c4c4c4c4c4c4c4ULL, },    /*  32  */
        { 0xc4c4c4c4c4c4c4c4ULL, 0xc4c4c4c4c4c4c4c4ULL, },
        { 0x4c4c4c4c4c4c4c4cULL, 0x4c4c4c4c4c4c4c4cULL, },
        { 0x9090909090909090ULL, 0x9090909090909090ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5c5c5c5c5c5c5c5cULL, 0x5c5c5c5c5c5c5c5cULL, },
        { 0x7834bc7834bc7834ULL, 0xbc7834bc7834bc78ULL, },
        { 0x2828282828282828ULL, 0x2828282828282828ULL, },
        { 0x5b5b5b5b5b5b5b5bULL, 0x5b5b5b5b5b5b5b5bULL, },    /*  40  */
        { 0x5b5b5b5b5b5b5b5bULL, 0x5b5b5b5b5b5b5b5bULL, },
        { 0x7d7d7d7d7d7d7d7dULL, 0x7d7d7d7d7d7d7d7dULL, },
        { 0x8e8e8e8e8e8e8e8eULL, 0x8e8e8e8e8e8e8e8eULL, },
        { 0xeaeaeaeaeaeaeaeaULL, 0xeaeaeaeaeaeaeaeaULL, },
        { 0xc1c1c1c1c1c1c1c1ULL, 0xc1c1c1c1c1c1c1c1ULL, },
        { 0x8877998877998877ULL, 0x9988779988779988ULL, },
        { 0xf4f4f4f4f4f4f4f4ULL, 0xf4f4f4f4f4f4f4f4ULL, },
        { 0xd7822cd7822cd782ULL, 0x2cd7822cd7822cd7ULL, },    /*  48  */
        { 0xd7822cd7822cd782ULL, 0x2cd7822cd7822cd7ULL, },
        { 0x1936fc1936fc1936ULL, 0xfc1936fc1936fc19ULL, },
        { 0xba1064ba1064ba10ULL, 0x64ba1064ba1064baULL, },
        { 0xd6e8c4d6e8c4d6e8ULL, 0xc4d6e8c4d6e8c4d6ULL, },
        { 0x9d9e9c9d9e9c9d9eULL, 0x9c9d9e9c9d9e9c9dULL, },
        { 0x54da5c54da5c54daULL, 0x5c54da5c54da5c54ULL, },
        { 0x802cd4802cd4802cULL, 0xd4802cd4802cd480ULL, },
        { 0x9c9d9b9c9d9b9c9dULL, 0x9b9c9d9b9c9d9b9cULL, },    /*  56  */
        { 0x9c9d9b9c9d9b9c9dULL, 0x9b9c9d9b9c9d9b9cULL, },
        { 0x0493750493750493ULL, 0x7504937504937504ULL, },
        { 0xb80e62b80e62b80eULL, 0x62b80e62b80e62b8ULL, },
        { 0x6802ce6802ce6802ULL, 0xce6802ce6802ce68ULL, },
        { 0xd47f29d47f29d47fULL, 0x29d47f29d47f29d4ULL, },
        { 0x00d1a100d1a100d1ULL, 0xa100d1a100d1a100ULL, },
        { 0xf0f0f0f0f0f0f0f0ULL, 0xf0f0f0f0f0f0f0f0ULL, },
        { 0xb00c4c60b06cb7f0ULL, 0xf77f776cecd7f060ULL, },    /*  64  */
        { 0x58604c7ca826a4f0ULL, 0xb11e6ee016929090ULL, },
        { 0xf81cf804c0e87df0ULL, 0x4436ec3e6ce920a0ULL, },
        { 0x786634a810267370ULL, 0xf53f14eebe33c020ULL, },
        { 0x20ba34c408e06070ULL, 0xafde0b62e8ee6050ULL, },
        { 0x07b6347bdf77af30ULL, 0x6b8d72be2f6d1c40ULL, },
        { 0x63ea34bd3a9aa230ULL, 0xad25d0d828d84290ULL, },
        { 0x934834f6f477f4c0ULL, 0xc39e78e84b9ade10ULL, },
        { 0x3304e07e0c39cdc0ULL, 0x56b6f646a1f16e20ULL, },    /*  72  */
        { 0x8f38e0c0675cc0c0ULL, 0x984e54609a5c9470ULL, },
        { 0xff949cdcb6fb47c0ULL, 0xa70e305f61233be0ULL, },
        { 0xbfcea8bac85c91c0ULL, 0x2cb600377e0d9160ULL, },
        { 0x3f18e45e189a8740ULL, 0xddbf28e7d05731e0ULL, },
        { 0x6f76e497d277d9d0ULL, 0xf338d0f7f319cd60ULL, },
        { 0x2fb0f075e4d823d0ULL, 0x78e0a0cf100323e0ULL, },
        { 0x2f4f0c4c60779f0cULL, 0xcfff608f7fff9fe0ULL, },
        { 0x379944bc60e9d40cULL, 0x2a66400d7d7a4f60ULL, },    /*  80  */
        { 0x4a0b4408801e08acULL, 0x36fc80bb3c7401e0ULL, },
        { 0x922d0cb800dcb0acULL, 0xfc5c807628f8dc60ULL, },
        { 0xb24a046000c05044ULL, 0x30c080e6c008a460ULL, },
        { 0x22a66ce00040c044ULL, 0x208000724030e4e0ULL, },
        { 0xcc726c4000808024ULL, 0xe00000de0060dc60ULL, },
        { 0xbc5e04c000000024ULL, 0xc00000bc004010e0ULL, },
        { 0x7c5cac000000002cULL, 0x0000001c00c0f0e0ULL, },
        { 0x9c4424000000002cULL, 0x000000d40080f060ULL, },    /*  88  */
        { 0xa8cc2400000000ccULL, 0x0000004c000010e0ULL, },
        { 0xc814ac00000000ccULL, 0x000000980000c060ULL, },
        { 0x48e8e400000000a4ULL, 0x0000005800004060ULL, },
        { 0x08d80c00000000a4ULL, 0x00000008000040e0ULL, },
        { 0x30880c0000000084ULL, 0x000000380000c060ULL, },
        { 0xf0b8e40000000084ULL, 0x00000070000000e0ULL, },
        { 0xf0f04c000000004cULL, 0x000000f0000000e0ULL, },
        { 0x709004000000004cULL, 0x000000d000000060ULL, },    /*  96  */
        { 0xf0f06c000000004cULL, 0x00000070000000e0ULL, },
        { 0x709064000000004cULL, 0x0000005000000060ULL, },
        { 0xf0f08c000000004cULL, 0x000000f0000000e0ULL, },
        { 0xa0d08c00000000ecULL, 0x0000009000000060ULL, },
        { 0xc0708c000000008cULL, 0x000000f0000000e0ULL, },
        { 0x80508c000000002cULL, 0x0000009000000060ULL, },
        { 0x00f08c00000000ccULL, 0x000000f0000000e0ULL, },
        { 0x00906400000000ccULL, 0x000000e000000060ULL, },    /* 104  */
        { 0x00f06c00000000ccULL, 0x000000c0000000e0ULL, },
        { 0x00900400000000ccULL, 0x0000008000000060ULL, },
        { 0x00f04c00000000ccULL, 0x00000000000000e0ULL, },
        { 0x00e0c400000000a4ULL, 0x00000000000000e0ULL, },
        { 0x00c0ec00000000acULL, 0x00000000000000e0ULL, },
        { 0x0080a40000000044ULL, 0x00000000000000e0ULL, },
        { 0x00008c000000008cULL, 0x00000000000000e0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MSUBV_B(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MSUBV_B(b128_random[i], b128_random[j],
                           b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                        (PATTERN_INPUTS_SHORT_COUNT)) +
                                       RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MSUBV_B__DDT(b128_random[i], b128_random[j],
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
            do_msa_MSUBV_B__DSD(b128_random[i], b128_random[j],
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
