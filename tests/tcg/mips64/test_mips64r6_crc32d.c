/*
 *  Test program for MIPS64R6 instruction CRC32D
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
    char *instruction_name =   "CRC32D";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b64_result[TEST_COUNT_TOTAL];
    uint64_t b64_expect[TEST_COUNT_TOTAL] = {
        0xffffffffdebb20e3ULL,                    /*   0  */
        0x0000000044660075ULL,
        0x000000001e20c2aeULL,
        0xffffffff84fde238ULL,
        0x00000000281d7ce7ULL,
        0xffffffffb2c05c71ULL,
        0xffffffffd660a024ULL,
        0x000000004cbd80b2ULL,
        0xffffffff9add2096ULL,                    /*   8  */
        0x0000000000000000ULL,
        0x000000005a46c2dbULL,
        0xffffffffc09be24dULL,
        0x000000006c7b7c92ULL,
        0xfffffffff6a65c04ULL,
        0xffffffff9206a051ULL,
        0x0000000008db80c7ULL,
        0x000000005449dd0fULL,                    /*  16  */
        0xffffffffce94fd99ULL,
        0xffffffff94d23f42ULL,
        0x000000000e0f1fd4ULL,
        0xffffffffa2ef810bULL,
        0x000000003832a19dULL,
        0x000000005c925dc8ULL,
        0xffffffffc64f7d5eULL,
        0x00000000102fdd7aULL,                    /*  24  */
        0xffffffff8af2fdecULL,
        0xffffffffd0b43f37ULL,
        0x000000004a691fa1ULL,
        0xffffffffe689817eULL,
        0x000000007c54a1e8ULL,
        0x0000000018f45dbdULL,
        0xffffffff82297d2bULL,
        0xffffffffa7157447ULL,                    /*  32  */
        0x000000003dc854d1ULL,
        0x00000000678e960aULL,
        0xfffffffffd53b69cULL,
        0x0000000051b32843ULL,
        0xffffffffcb6e08d5ULL,
        0xffffffffafcef480ULL,
        0x000000003513d416ULL,
        0xffffffffe3737432ULL,                    /*  40  */
        0x0000000079ae54a4ULL,
        0x0000000023e8967fULL,
        0xffffffffb935b6e9ULL,
        0x0000000015d52836ULL,
        0xffffffff8f0808a0ULL,
        0xffffffffeba8f4f5ULL,
        0x000000007175d463ULL,
        0x000000007a6adc3eULL,                    /*  48  */
        0xffffffffe0b7fca8ULL,
        0xffffffffbaf13e73ULL,
        0x00000000202c1ee5ULL,
        0xffffffff8ccc803aULL,
        0x000000001611a0acULL,
        0x0000000072b15cf9ULL,
        0xffffffffe86c7c6fULL,
        0x000000003e0cdc4bULL,                    /*  56  */
        0xffffffffa4d1fcddULL,
        0xfffffffffe973e06ULL,
        0x00000000644a1e90ULL,
        0xffffffffc8aa804fULL,
        0x000000005277a0d9ULL,
        0x0000000036d75c8cULL,
        0xffffffffac0a7c1aULL,
        0xffffffffed857593ULL,                    /*  64  */
        0xffffffffe0b6f95fULL,
        0x00000000253b462cULL,
        0xffffffffe15579b9ULL,
        0x0000000074897c83ULL,
        0x0000000079baf04fULL,
        0xffffffffbc374f3cULL,
        0x00000000785970a9ULL,
        0xffffffffa6bae0a9ULL,                    /*  72  */
        0xffffffffab896c65ULL,
        0x000000006e04d316ULL,
        0xffffffffaa6aec83ULL,
        0x000000005ae171feULL,
        0x0000000057d2fd32ULL,
        0xffffffff925f4241ULL,
        0x0000000056317dd4ULL,
    };

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_CRC32D(b64_pattern + i, b64_pattern + j,
                b64_result + (PATTERN_INPUTS_64_SHORT_COUNT * i + j));
        }
    }

    for (i = 0; i < RANDOM_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_CRC32D(b64_random + i, b64_random + j,
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
