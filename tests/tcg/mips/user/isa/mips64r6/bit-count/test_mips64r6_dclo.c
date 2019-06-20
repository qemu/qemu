/*
 *  Test program for MIPS64R6 instruction DCLO
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

#include "../../../../include/wrappers_mips64r6.h"
#include "../../../../include/test_inputs_64.h"
#include "../../../../include/test_utils_64.h"

#define TEST_COUNT_TOTAL (PATTERN_INPUTS_64_COUNT + RANDOM_INPUTS_64_COUNT)


int32_t main(void)
{
    char *isa_ase_name = "mips64r6";
    char *group_name = "Bit Count";
    char *instruction_name =   "DCLO";
    int32_t ret;
    uint32_t i;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b64_result[TEST_COUNT_TOTAL];
    uint64_t b64_expect[TEST_COUNT_TOTAL] = {
        0x0000000000000040ULL,                    /*   0  */
        0x0000000000000000ULL,
        0x0000000000000001ULL,
        0x0000000000000000ULL,
        0x0000000000000002ULL,
        0x0000000000000000ULL,
        0x0000000000000003ULL,
        0x0000000000000000ULL,
        0x0000000000000004ULL,                    /*   8  */
        0x0000000000000000ULL,
        0x0000000000000005ULL,
        0x0000000000000000ULL,
        0x0000000000000006ULL,
        0x0000000000000000ULL,
        0x0000000000000007ULL,
        0x0000000000000000ULL,
        0x0000000000000008ULL,                    /*  16  */
        0x0000000000000000ULL,
        0x0000000000000009ULL,
        0x0000000000000000ULL,
        0x000000000000000aULL,
        0x0000000000000000ULL,
        0x000000000000000bULL,
        0x0000000000000000ULL,
        0x000000000000000cULL,                    /*  24  */
        0x0000000000000000ULL,
        0x000000000000000dULL,
        0x0000000000000000ULL,
        0x000000000000000eULL,
        0x0000000000000000ULL,
        0x000000000000000fULL,
        0x0000000000000000ULL,
        0x0000000000000010ULL,                    /*  32  */
        0x0000000000000000ULL,
        0x0000000000000011ULL,
        0x0000000000000000ULL,
        0x0000000000000012ULL,
        0x0000000000000000ULL,
        0x0000000000000013ULL,
        0x0000000000000000ULL,
        0x0000000000000014ULL,                    /*  40  */
        0x0000000000000000ULL,
        0x0000000000000015ULL,
        0x0000000000000000ULL,
        0x0000000000000016ULL,
        0x0000000000000000ULL,
        0x0000000000000017ULL,
        0x0000000000000000ULL,
        0x0000000000000018ULL,                    /*  48  */
        0x0000000000000000ULL,
        0x0000000000000019ULL,
        0x0000000000000000ULL,
        0x000000000000001aULL,
        0x0000000000000000ULL,
        0x000000000000001bULL,
        0x0000000000000000ULL,
        0x000000000000001cULL,                    /*  56  */
        0x0000000000000000ULL,
        0x000000000000001dULL,
        0x0000000000000000ULL,
        0x000000000000001eULL,
        0x0000000000000000ULL,
        0x000000000000001fULL,
        0x0000000000000000ULL,
        0x0000000000000001ULL,                    /*  64  */
        0x0000000000000005ULL,
        0x0000000000000001ULL,
        0x0000000000000000ULL,
        0x0000000000000001ULL,
        0x0000000000000002ULL,
        0x0000000000000001ULL,
        0x0000000000000006ULL,
        0x0000000000000000ULL,                    /*  72  */
        0x0000000000000001ULL,
        0x0000000000000001ULL,
        0x0000000000000001ULL,
        0x0000000000000003ULL,
        0x0000000000000001ULL,
        0x0000000000000000ULL,
        0x0000000000000001ULL,
    };

    gettimeofday(&start, NULL);

    for (i = 0; i < TEST_COUNT_TOTAL; i++) {
        if (i < PATTERN_INPUTS_64_COUNT) {
            do_mips64r6_DCLO((void *)&b64_pattern[i], (void *)&b64_result[i]);
        } else {
            do_mips64r6_DCLO((void *)&b64_random[i - PATTERN_INPUTS_64_COUNT],
                             (void *)&b64_result[i]);
        }
    }

    gettimeofday(&end, NULL);

    elapsed_time = (end.tv_sec - start.tv_sec) * 1000.0;
    elapsed_time += (end.tv_usec - start.tv_usec) / 1000.0;

    ret = check_results_64(instruction_name, TEST_COUNT_TOTAL, elapsed_time,
                           b64_result, b64_expect);

    return ret;
}
