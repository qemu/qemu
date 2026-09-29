/*
 *  Test program for MIPS64R6 instruction CRC32B
 *
 *  Copyright (C) 2019  Wave Computing, Inc.
 *  Copyright (C) 2019  Aleksandar Markovic <amarkovic@wavecomp.com>
 *  Copyright (C) 2025  Aleksandar Rakic <aleksandar.rakic@htecgroup.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <sys/time.h>
#include <stdint.h>

#include "../../../../include/wrappers_mips64r6.h"
#include "../../../../include/test_inputs_64.h"
#include "../../../../include/test_utils_64.h"

#define TEST_COUNT_TOTAL (PATTERN_INPUTS_64_COUNT + RANDOM_INPUTS_64_COUNT)

int32_t main(void)
{
    char *isa_ase_name = "mips64r6";
    char *group_name = "CRC with reversed polynomial 0xEDB88320";
    char *instruction_name =   "CRC32B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b64_result[TEST_COUNT_TOTAL];
    uint64_t b64_expect[TEST_COUNT_TOTAL] = {
        0x0000000000ffffffULL,                    /*   0  */
        0x000000002d02ef8dULL,
        0x000000001bab0fd1ULL,
        0x0000000036561fa3ULL,
        0xffffffffbf1caddaULL,
        0xffffffff92e1bda8ULL,
        0x00000000278c7949ULL,
        0x000000000a71693bULL,
        0x000000002dfd1072ULL,                    /*   8  */
        0x0000000000000000ULL,
        0x0000000036a9e05cULL,
        0x000000001b54f02eULL,
        0xffffffff921e4257ULL,
        0xffffffffbfe35225ULL,
        0x000000000a8e96c4ULL,
        0x00000000277386b6ULL,
        0x000000001bfe5a84ULL,                    /*  16  */
        0x0000000036034af6ULL,
        0x0000000000aaaaaaULL,
        0x000000002d57bad8ULL,
        0xffffffffa41d08a1ULL,
        0xffffffff89e018d3ULL,
        0x000000003c8ddc32ULL,
        0x000000001170cc40ULL,
        0x0000000036fcb509ULL,                    /*  24  */
        0x000000001b01a57bULL,
        0x000000002da84527ULL,
        0x0000000000555555ULL,
        0xffffffff891fe72cULL,
        0xffffffffa4e2f75eULL,
        0x00000000118f33bfULL,
        0x000000003c7223cdULL,
        0xffffffffbf2f9ee9ULL,                    /*  32  */
        0xffffffff92d28e9bULL,
        0xffffffffa47b6ec7ULL,
        0xffffffff89867eb5ULL,
        0x0000000000ccccccULL,
        0x000000002d31dcbeULL,
        0xffffffff985c185fULL,
        0xffffffffb5a1082dULL,
        0xffffffff922d7164ULL,                    /*  40  */
        0xffffffffbfd06116ULL,
        0xffffffff8979814aULL,
        0xffffffffa4849138ULL,
        0x000000002dce2341ULL,
        0x0000000000333333ULL,
        0xffffffffb55ef7d2ULL,
        0xffffffff98a3e7a0ULL,
        0x0000000027fdbe55ULL,                    /*  48  */
        0x000000000a00ae27ULL,
        0x000000003ca94e7bULL,
        0x0000000011545e09ULL,
        0xffffffff981eec70ULL,
        0xffffffffb5e3fc02ULL,
        0x00000000008e38e3ULL,
        0x000000002d732891ULL,
        0x000000000aff51d8ULL,                    /*  56  */
        0x00000000270241aaULL,
        0x0000000011aba1f6ULL,
        0x000000003c56b184ULL,
        0xffffffffb51c03fdULL,
        0xffffffff98e1138fULL,
        0x000000002d8cd76eULL,
        0x000000000071c71cULL,
        0x0000000000286255ULL,                    /*  64  */
        0x00000000784a5a65ULL,
        0xffffffff9bdd0d3bULL,
        0xffffffffe7e61ce5ULL,
        0x00000000782fabf7ULL,
        0x00000000004d93c7ULL,
        0xffffffffe3dac499ULL,
        0xffffffff9fe1d547ULL,
        0xffffffff9b4ca0e5ULL,                    /*  72  */
        0xffffffffe32e98d5ULL,
        0x0000000000b9cf8bULL,
        0x000000007c82de55ULL,
        0xffffffffe7904f52ULL,
        0xffffffff9ff27762ULL,
        0x000000007c65203cULL,
        0x00000000005e31e2ULL,
    };

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_CRC32B(b64_pattern + i, b64_pattern + j,
                b64_result + (PATTERN_INPUTS_64_SHORT_COUNT * i + j));
        }
    }

    for (i = 0; i < RANDOM_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_CRC32B(b64_random + i, b64_random + j,
                b64_result + (((PATTERN_INPUTS_64_SHORT_COUNT) *
                               (PATTERN_INPUTS_64_SHORT_COUNT)) +
                              RANDOM_INPUTS_64_SHORT_COUNT * i + j));
        }
    }

    gettimeofday(&end, NULL);

    elapsed_time = (end.tv_sec - start.tv_sec) * 1000.0;
    elapsed_time += (end.tv_usec - start.tv_usec) / 1000.0;

    ret = check_results_64(isa_ase_name, group_name, instruction_name,
                           TEST_COUNT_TOTAL, elapsed_time, b64_result,
                           b64_expect);

    return ret;
}
