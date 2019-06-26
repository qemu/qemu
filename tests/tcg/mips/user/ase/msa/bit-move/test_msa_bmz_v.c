/*
 *  Test program for MSA instruction BMZ.V
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
    char *group_name = "Bit Move";
    char *instruction_name =  "BMZ.V";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x0860c60c20421440ULL, 0x430401461c71800cULL, },    /*  64  */
        { 0x0860e68c20621440ULL, 0x4b040146fe71a00cULL, },
        { 0x0860e6cc20625440ULL, 0x4b270946fe71b00cULL, },
        { 0x8860e6cc20625540ULL, 0x4b270b46fe79b00cULL, },
        { 0xfbf4e6ef65f3d748ULL, 0x5bb7bb46ff7df2fcULL, },
        { 0xfbb400634593c708ULL, 0x12b7bb02153d52fcULL, },
        { 0xfbb400634593c708ULL, 0x12b7bb02153d52fcULL, },
        { 0xfbb400634593c708ULL, 0x12b7bb02153d52fcULL, },
        { 0xac300862918fcf80ULL, 0x26bfcfa31539151cULL, },    /*  72  */
        { 0xac70aeeab1cfcf80ULL, 0x27bfcfe7bf39351cULL, },
        { 0xac50aeaab1cf8b80ULL, 0x2798c6e7ab292514ULL, },
        { 0xac50aeaab1cf8b80ULL, 0x2798c6e7ab292514ULL, },
        { 0xf845b6897653a30eULL, 0x879082c6ab2962a4ULL, },
        { 0xf845160d5633a34eULL, 0x8f9082c2a969e2a4ULL, },
        { 0xf845164d5633e34eULL, 0x8fb18ac2a969e2a4ULL, },
        { 0x7045164d5631e24eULL, 0x8db188c0a940e2a0ULL, },
        { 0x7045164d5631e24eULL, 0x8db188c0a940e2a0ULL, },    /*  80  */
        { 0x7045164d5631e24eULL, 0x8db188c0a940e2a0ULL, },
        { 0x7045164d5631e24eULL, 0x8db188c0a940e2a0ULL, },
        { 0x7045164d5631e24eULL, 0x8db188c0a940e2a0ULL, },
        { 0x7045164d5631e24eULL, 0x8db188c0a940e2a0ULL, },
        { 0x7045164d5631e24eULL, 0x8db188c0a940e2a0ULL, },
        { 0x7045164d5631e24eULL, 0x8db188c0a940e2a0ULL, },
        { 0x7045164d5631e24eULL, 0x8db188c0a940e2a0ULL, },
        { 0x7045164d5631e24eULL, 0x8db188c0a940e2a0ULL, },    /*  88  */
        { 0x7045164d5631e24eULL, 0x8db188c0a940e2a0ULL, },
        { 0x7045164d5631e24eULL, 0x8db188c0a940e2a0ULL, },
        { 0x7045164d5631e24eULL, 0x8db188c0a940e2a0ULL, },
        { 0x7045164d5631e24eULL, 0x8db188c0a940e2a0ULL, },
        { 0x7045164d5631e24eULL, 0x8db188c0a940e2a0ULL, },
        { 0x7045164d5631e24eULL, 0x8db188c0a940e2a0ULL, },
        { 0x7045164d5631e24eULL, 0x8db188c0a940e2a0ULL, },
        { 0xf86ff6cd7e73f74eULL, 0xcff78bdeff7bf2acULL, },    /*  96  */
        { 0xf86ff6cd7e73f74eULL, 0xcff78bdeff7bf2acULL, },
        { 0xf86ff6cd7e73f74eULL, 0xcff78bdeff7bf2acULL, },
        { 0xf86ff6cd7e73f74eULL, 0xcff78bdeff7bf2acULL, },
        { 0xfbfff6ef7ff3f74eULL, 0xdff7bbdeff7ff2fcULL, },
        { 0xfbfff6ef7ff3f74eULL, 0xdff7bbdeff7ff2fcULL, },
        { 0xfbfff6ef7ff3f74eULL, 0xdff7bbdeff7ff2fcULL, },
        { 0xfbfff6ef7ff3f74eULL, 0xdff7bbdeff7ff2fcULL, },
        { 0xfffffeefffffffceULL, 0xffffffffff7ff7fcULL, },    /* 104  */
        { 0xfffffeefffffffceULL, 0xffffffffff7ff7fcULL, },
        { 0xfffffeefffffffceULL, 0xffffffffff7ff7fcULL, },
        { 0xfffffeefffffffceULL, 0xffffffffff7ff7fcULL, },
        { 0xfffffeefffffffceULL, 0xffffffffff7ff7fcULL, },
        { 0xfffffeefffffffceULL, 0xffffffffff7ff7fcULL, },
        { 0xfffffeefffffffceULL, 0xffffffffff7ff7fcULL, },
        { 0xfffffeefffffffceULL, 0xffffffffff7ff7fcULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_BMZ_V(b128_pattern[i], b128_pattern[j],
                         b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_BMZ_V(b128_random[i], b128_random[j],
                         b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                      (PATTERN_INPUTS_SHORT_COUNT)) +
                                     RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_BMZ_V__DDT(b128_random[i], b128_random[j],
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
            do_msa_BMZ_V__DSD(b128_random[i], b128_random[j],
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
