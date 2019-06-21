/*
 *  Test program for MSA instruction HADD_S.W
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
            (RANDOM_INPUTS_SHORT_COUNT) * (RANDOM_INPUTS_SHORT_COUNT))


int32_t main(void)
{
    char *isa_ase_name = "MSA";
    char *group_name = "Int Add";
    char *instruction_name =  "HADD_S.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xfffffffefffffffeULL, 0xfffffffefffffffeULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffaaa9ffffaaa9ULL, 0xffffaaa9ffffaaa9ULL, },
        { 0x0000555400005554ULL, 0x0000555400005554ULL, },
        { 0xffffcccbffffcccbULL, 0xffffcccbffffcccbULL, },
        { 0x0000333200003332ULL, 0x0000333200003332ULL, },
        { 0x000038e2ffffe38dULL, 0xffff8e37000038e2ULL, },
        { 0xffffc71b00001c70ULL, 0x000071c6ffffc71bULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffaaaaffffaaaaULL, 0xffffaaaaffffaaaaULL, },
        { 0x0000555500005555ULL, 0x0000555500005555ULL, },
        { 0xffffccccffffccccULL, 0xffffccccffffccccULL, },
        { 0x0000333300003333ULL, 0x0000333300003333ULL, },
        { 0x000038e3ffffe38eULL, 0xffff8e38000038e3ULL, },
        { 0xffffc71c00001c71ULL, 0x000071c7ffffc71cULL, },
        { 0xffffaaa9ffffaaa9ULL, 0xffffaaa9ffffaaa9ULL, },    /*  16  */
        { 0xffffaaaaffffaaaaULL, 0xffffaaaaffffaaaaULL, },
        { 0xffff5554ffff5554ULL, 0xffff5554ffff5554ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffff7776ffff7776ULL, 0xffff7776ffff7776ULL, },
        { 0xffffddddffffddddULL, 0xffffddddffffddddULL, },
        { 0xffffe38dffff8e38ULL, 0xffff38e2ffffe38dULL, },
        { 0xffff71c6ffffc71bULL, 0x00001c71ffff71c6ULL, },
        { 0x0000555400005554ULL, 0x0000555400005554ULL, },    /*  24  */
        { 0x0000555500005555ULL, 0x0000555500005555ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000aaaa0000aaaaULL, 0x0000aaaa0000aaaaULL, },
        { 0x0000222100002221ULL, 0x0000222100002221ULL, },
        { 0x0000888800008888ULL, 0x0000888800008888ULL, },
        { 0x00008e38000038e3ULL, 0xffffe38d00008e38ULL, },
        { 0x00001c71000071c6ULL, 0x0000c71c00001c71ULL, },
        { 0xffffcccbffffcccbULL, 0xffffcccbffffcccbULL, },    /*  32  */
        { 0xffffccccffffccccULL, 0xffffccccffffccccULL, },
        { 0xffff7776ffff7776ULL, 0xffff7776ffff7776ULL, },
        { 0x0000222100002221ULL, 0x0000222100002221ULL, },
        { 0xffff9998ffff9998ULL, 0xffff9998ffff9998ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x000005afffffb05aULL, 0xffff5b04000005afULL, },
        { 0xffff93e8ffffe93dULL, 0x00003e93ffff93e8ULL, },
        { 0x0000333200003332ULL, 0x0000333200003332ULL, },    /*  40  */
        { 0x0000333300003333ULL, 0x0000333300003333ULL, },
        { 0xffffddddffffddddULL, 0xffffddddffffddddULL, },
        { 0x0000888800008888ULL, 0x0000888800008888ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0000666600006666ULL, 0x0000666600006666ULL, },
        { 0x00006c16000016c1ULL, 0xffffc16b00006c16ULL, },
        { 0xfffffa4f00004fa4ULL, 0x0000a4fafffffa4fULL, },
        { 0xffffe38dffff8e37ULL, 0x000038e2ffffe38dULL, },    /*  48  */
        { 0xffffe38effff8e38ULL, 0x000038e3ffffe38eULL, },
        { 0xffff8e38ffff38e2ULL, 0xffffe38dffff8e38ULL, },
        { 0x000038e3ffffe38dULL, 0x00008e38000038e3ULL, },
        { 0xffffb05affff5b04ULL, 0x000005afffffb05aULL, },
        { 0x000016c1ffffc16bULL, 0x00006c16000016c1ULL, },
        { 0x00001c71ffff71c6ULL, 0xffffc71b00001c71ULL, },
        { 0xffffaaaaffffaaa9ULL, 0x0000aaaaffffaaaaULL, },
        { 0x00001c70000071c6ULL, 0xffffc71b00001c70ULL, },    /*  56  */
        { 0x00001c71000071c7ULL, 0xffffc71c00001c71ULL, },
        { 0xffffc71b00001c71ULL, 0xffff71c6ffffc71bULL, },
        { 0x000071c60000c71cULL, 0x00001c71000071c6ULL, },
        { 0xffffe93d00003e93ULL, 0xffff93e8ffffe93dULL, },
        { 0x00004fa40000a4faULL, 0xfffffa4f00004fa4ULL, },
        { 0x0000555400005555ULL, 0xffff555400005554ULL, },
        { 0xffffe38d00008e38ULL, 0x000038e3ffffe38dULL, },
        { 0xffff6f3600007da2ULL, 0x000056c5ffffae87ULL, },    /*  64  */
        { 0xffff88cdffffef6aULL, 0x0000068100005177ULL, },
        { 0xffff3714ffffb3e2ULL, 0x000012660000238fULL, },
        { 0xffff9eb700000ab0ULL, 0xffffd43fffffe11bULL, },
        { 0xffffe28a0000a2d3ULL, 0x00001e55ffffc54bULL, },
        { 0xfffffc210000149bULL, 0xffffce110000683bULL, },
        { 0xffffaa68ffffd913ULL, 0xffffd9f600003a53ULL, },
        { 0x0000120b00002fe1ULL, 0xffff9bcffffff7dfULL, },
        { 0xffff932600000f0fULL, 0x00003336ffff5b37ULL, },    /*  72  */
        { 0xffffacbdffff80d7ULL, 0xffffe2f2fffffe27ULL, },
        { 0xffff5b04ffff454fULL, 0xffffeed7ffffd03fULL, },
        { 0xffffc2a7ffff9c1dULL, 0xffffb0b0ffff8dcbULL, },
        { 0x0000571b0000b371ULL, 0xffff994fffff594eULL, },
        { 0x000070b200002539ULL, 0xffff490bfffffc3eULL, },
        { 0x00001ef9ffffe9b1ULL, 0xffff54f0ffffce56ULL, },
        { 0x0000869c0000407fULL, 0xffff16c9ffff8be2ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_HADD_S_W(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_HADD_S_W(b128_random[i], b128_random[j],
                           b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                        (PATTERN_INPUTS_SHORT_COUNT)) +
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
