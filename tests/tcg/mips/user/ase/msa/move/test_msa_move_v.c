/*
 *  Test program for MSA instruction MOVE.V
 *
 *  Copyright (C) 2019  Wave Computing, Inc.
 *  Copyright (C) 2019  Aleksandar Markovic <amarkovic@wavecomp.com>
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

#define TEST_COUNT_TOTAL (PATTERN_INPUTS_COUNT + RANDOM_INPUTS_COUNT)


int32_t main(void)
{
    char *isa_ase_name = "MSA";
    char *group_name = "Move";
    char *instruction_name =  "MOVE.V";
    int32_t ret;
    uint32_t i;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0xf0f0f0f0f0f0f0f0ULL, 0xf0f0f0f0f0f0f0f0ULL, },    /*   8  */
        { 0x0f0f0f0f0f0f0f0fULL, 0x0f0f0f0f0f0f0f0fULL, },
        { 0xf83e0f83e0f83e0fULL, 0x83e0f83e0f83e0f8ULL, },
        { 0x07c1f07c1f07c1f0ULL, 0x7c1f07c1f07c1f07ULL, },
        { 0xfc0fc0fc0fc0fc0fULL, 0xc0fc0fc0fc0fc0fcULL, },
        { 0x03f03f03f03f03f0ULL, 0x3f03f03f03f03f03ULL, },
        { 0xfe03f80fe03f80feULL, 0x03f80fe03f80fe03ULL, },
        { 0x01fc07f01fc07f01ULL, 0xfc07f01fc07f01fcULL, },
        { 0xff00ff00ff00ff00ULL, 0xff00ff00ff00ff00ULL, },    /*  16  */
        { 0x00ff00ff00ff00ffULL, 0x00ff00ff00ff00ffULL, },
        { 0xff803fe00ff803feULL, 0x00ff803fe00ff803ULL, },
        { 0x007fc01ff007fc01ULL, 0xff007fc01ff007fcULL, },
        { 0xffc00ffc00ffc00fULL, 0xfc00ffc00ffc00ffULL, },
        { 0x003ff003ff003ff0ULL, 0x03ff003ff003ff00ULL, },
        { 0xffe003ff800ffe00ULL, 0x3ff800ffe003ff80ULL, },
        { 0x001ffc007ff001ffULL, 0xc007ff001ffc007fULL, },
        { 0xfff000fff000fff0ULL, 0x00fff000fff000ffULL, },    /*  24  */
        { 0x000fff000fff000fULL, 0xff000fff000fff00ULL, },
        { 0xfff8003ffe000fffULL, 0x8003ffe000fff800ULL, },
        { 0x0007ffc001fff000ULL, 0x7ffc001fff0007ffULL, },
        { 0xfffc000fffc000ffULL, 0xfc000fffc000fffcULL, },
        { 0x0003fff0003fff00ULL, 0x03fff0003fff0003ULL, },
        { 0xfffe0003fff8000fULL, 0xffe0003fff8000ffULL, },
        { 0x0001fffc0007fff0ULL, 0x001fffc0007fff00ULL, },
        { 0xffff0000ffff0000ULL, 0xffff0000ffff0000ULL, },    /*  32  */
        { 0x0000ffff0000ffffULL, 0x0000ffff0000ffffULL, },
        { 0xffff80003fffe000ULL, 0x0ffff80003fffe00ULL, },
        { 0x00007fffc0001fffULL, 0xf00007fffc0001ffULL, },
        { 0xffffc0000ffffc00ULL, 0x00ffffc0000ffffcULL, },
        { 0x00003ffff00003ffULL, 0xff00003ffff00003ULL, },
        { 0xffffe00003ffff80ULL, 0x000ffffe00003fffULL, },
        { 0x00001ffffc00007fULL, 0xfff00001ffffc000ULL, },
        { 0xfffff00000fffff0ULL, 0x0000fffff00000ffULL, },    /*  40  */
        { 0x00000fffff00000fULL, 0xffff00000fffff00ULL, },
        { 0xfffff800003ffffeULL, 0x00000fffff800003ULL, },
        { 0x000007ffffc00001ULL, 0xfffff000007ffffcULL, },
        { 0xfffffc00000fffffULL, 0xc00000fffffc0000ULL, },
        { 0x000003fffff00000ULL, 0x3fffff000003ffffULL, },
        { 0xfffffe000003ffffULL, 0xf800000fffffe000ULL, },
        { 0x000001fffffc0000ULL, 0x07fffff000001fffULL, },
        { 0xffffff000000ffffULL, 0xff000000ffffff00ULL, },    /*  48  */
        { 0x000000ffffff0000ULL, 0x00ffffff000000ffULL, },
        { 0xffffff8000003fffULL, 0xffe000000ffffff8ULL, },
        { 0x0000007fffffc000ULL, 0x001ffffff0000007ULL, },
        { 0xffffffc000000fffULL, 0xfffc000000ffffffULL, },
        { 0x0000003ffffff000ULL, 0x0003ffffff000000ULL, },
        { 0xffffffe0000003ffULL, 0xffff8000000fffffULL, },
        { 0x0000001ffffffc00ULL, 0x00007ffffff00000ULL, },
        { 0xfffffff0000000ffULL, 0xfffff0000000ffffULL, },    /*  56  */
        { 0x0000000fffffff00ULL, 0x00000fffffff0000ULL, },
        { 0xfffffff80000003fULL, 0xfffffe0000000fffULL, },
        { 0x00000007ffffffc0ULL, 0x000001fffffff000ULL, },
        { 0xfffffffc0000000fULL, 0xffffffc0000000ffULL, },
        { 0x00000003fffffff0ULL, 0x0000003fffffff00ULL, },
        { 0xfffffffe00000003ULL, 0xfffffff80000000fULL, },
        { 0x00000001fffffffcULL, 0x00000007fffffff0ULL, },
        { 0x886ae6cc28625540ULL, 0x4b670b5efe7bb00cULL, },    /*  64  */
        { 0xfbbe00634d93c708ULL, 0x12f7bb1a153f52fcULL, },
        { 0xac5aaeaab9cf8b80ULL, 0x27d8c6ffab2b2514ULL, },
        { 0x704f164d5e31e24eULL, 0x8df188d8a942e2a0ULL, },
        { 0xb9926b7c7daf4258ULL, 0xa1227caddcce65b6ULL, },
        { 0xd027be89ff0a2ef9ULL, 0x170b5050fea53078ULL, },
        { 0xb83b580665cabc4aULL, 0x91230822bff0ba62ULL, },
        { 0xfc8f23f09aa6b782ULL, 0x93fd6637124275aeULL, },
        { 0x201e09cd56aee649ULL, 0xef5de039a6a52758ULL, },    /*  72  */
        { 0xa57cd91365d9e5d7ULL, 0x9321bc9881ecba5cULL, },
        { 0xa2e8f6f5c9cbc61bULL, 0xb2c471545e0d7a12ULL, },
        { 0xa89cf2f131a864aeULL, 0xd2a3e87a5db986e7ULL, },
        { 0xe61438e9a652ea0aULL, 0xa85483d97879d41cULL, },
        { 0x944a35fd192361a8ULL, 0xf3912da36a0b2d6bULL, },
        { 0x4630426322bef79cULL, 0xeb5686f7cb19304eULL, },
        { 0x8b5aa7a2f259deadULL, 0xd278cbcd696417e3ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < TEST_COUNT_TOTAL; i++) {
        if (i < PATTERN_INPUTS_COUNT) {
            do_msa_MOVE_V(b128_pattern[i], b128_result[i]);
        } else {
            do_msa_MOVE_V(b128_random[i - PATTERN_INPUTS_COUNT],
                          b128_result[i]);
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
