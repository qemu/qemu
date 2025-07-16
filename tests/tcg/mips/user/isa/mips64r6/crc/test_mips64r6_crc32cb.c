/*
 *  Test program for MIPS64R6 instruction CRC32CB
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
    char *group_name = "CRC with reversed polynomial 0x82F63B78";
    char *instruction_name =   "CRC32CB";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b64_result[TEST_COUNT_TOTAL];
    uint64_t b64_expect[TEST_COUNT_TOTAL] = {
        0x0000000000ffffffULL,                    /*   0  */
        0xffffffffad7d5351ULL,
        0x00000000647e6465ULL,
        0xffffffffc9fcc8cbULL,
        0x00000000237f7689ULL,
        0xffffffff8efdda27ULL,
        0xffffffff837defedULL,
        0x000000002eff4343ULL,
        0xffffffffad82acaeULL,                    /*   8  */
        0x0000000000000000ULL,
        0xffffffffc9033734ULL,
        0x0000000064819b9aULL,
        0xffffffff8e0225d8ULL,
        0x0000000023808976ULL,
        0x000000002e00bcbcULL,
        0xffffffff83821012ULL,
        0x00000000642b3130ULL,                    /*  16  */
        0xffffffffc9a99d9eULL,
        0x0000000000aaaaaaULL,
        0xffffffffad280604ULL,
        0x0000000047abb846ULL,
        0xffffffffea2914e8ULL,
        0xffffffffe7a92122ULL,
        0x000000004a2b8d8cULL,
        0xffffffffc9566261ULL,                    /*  24  */
        0x0000000064d4cecfULL,
        0xffffffffadd7f9fbULL,
        0x0000000000555555ULL,
        0xffffffffead6eb17ULL,
        0x00000000475447b9ULL,
        0x000000004ad47273ULL,
        0xffffffffe756deddULL,
        0x00000000234c45baULL,                    /*  32  */
        0xffffffff8ecee914ULL,
        0x0000000047cdde20ULL,
        0xffffffffea4f728eULL,
        0x0000000000ccccccULL,
        0xffffffffad4e6062ULL,
        0xffffffffa0ce55a8ULL,
        0x000000000d4cf906ULL,
        0xffffffff8e3116ebULL,                    /*  40  */
        0x0000000023b3ba45ULL,
        0xffffffffeab08d71ULL,
        0x00000000473221dfULL,
        0xffffffffadb19f9dULL,
        0x0000000000333333ULL,
        0x000000000db306f9ULL,
        0xffffffffa031aa57ULL,
        0xffffffff830c28f1ULL,                    /*  48  */
        0x000000002e8e845fULL,
        0xffffffffe78db36bULL,
        0x000000004a0f1fc5ULL,
        0xffffffffa08ca187ULL,
        0x000000000d0e0d29ULL,
        0x00000000008e38e3ULL,
        0xffffffffad0c944dULL,
        0x000000002e717ba0ULL,                    /*  56  */
        0xffffffff83f3d70eULL,
        0x000000004af0e03aULL,
        0xffffffffe7724c94ULL,
        0x000000000df1f2d6ULL,
        0xffffffffa0735e78ULL,
        0xffffffffadf36bb2ULL,
        0x000000000071c71cULL,
        0x0000000000286255ULL,                    /*  64  */
        0xffffffffcbefd6b4ULL,
        0xffffffffc334e94fULL,
        0xffffffffac268ec5ULL,
        0xffffffffcb8a2726ULL,
        0x00000000004d93c7ULL,
        0x000000000896ac3cULL,
        0x000000006784cbb6ULL,
        0xffffffffc3a54491ULL,                    /*  72  */
        0x000000000862f070ULL,
        0x0000000000b9cf8bULL,
        0x000000006faba801ULL,
        0xffffffffac50dd72ULL,
        0x0000000067976993ULL,
        0x000000006f4c5668ULL,
        0x00000000005e31e2ULL,
    };

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_CRC32CB(b64_pattern + i, b64_pattern + j,
                b64_result + (PATTERN_INPUTS_64_SHORT_COUNT * i + j));
        }
    }

    for (i = 0; i < RANDOM_INPUTS_64_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_64_SHORT_COUNT; j++) {
            do_mips64r6_CRC32CB(b64_random + i, b64_random + j,
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
