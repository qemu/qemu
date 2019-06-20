/*
 *  Header file for test utilities
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

#ifndef TEST_UTILS_128_H
#define TEST_UTILS_128_H

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#define PRINT_RESULTS 0


static inline int32_t check_results_128(const char *isa_ase_name,
                                        const char *group_name,
                                        const char *instruction_name,
                                        const uint32_t test_count,
                                        const double elapsed_time,
                                        const uint64_t *b128_result,
                                        const uint64_t *b128_expect)
{
#if PRINT_RESULTS
    uint32_t ii;
    printf("\n");
    for (ii = 0; ii < test_count; ii++) {
        uint64_t a, b;
        memcpy(&a, (b128_result + 2 * ii), 8);
        memcpy(&b, (b128_result + 2 * ii + 1), 8);
        if (ii % 8 != 0) {
            printf("        { 0x%016llxULL, 0x%016llxULL, },\n", a, b);
        } else {
            printf("        { 0x%016llxULL, 0x%016llxULL, },    /* %3d  */\n",
                   a, b, ii);
        }
    }
    printf("\n");
#endif
    uint32_t i;
    uint32_t pass_count = 0;
    uint32_t fail_count = 0;

    printf("| %-10s \t| %-20s\t| %-16s \t|",
           isa_ase_name, group_name, instruction_name);
    for (i = 0; i < test_count; i++) {
        if ((b128_result[2 * i] == b128_expect[2 * i]) &&
            (b128_result[2 * i + 1] == b128_expect[2 * i + 1])) {
            pass_count++;
        } else {
            fail_count++;
        }
    }

    printf(" PASS: %3d \t| FAIL: %3d \t| elapsed time: %5.2f ms \t|\n",
           pass_count, fail_count, elapsed_time);

    if (fail_count > 0) {
        return -1;
    } else {
        return 0;
    }
}

#endif
