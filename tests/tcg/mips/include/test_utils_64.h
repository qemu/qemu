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

#ifndef TEST_UTILS_64_H
#define TEST_UTILS_64_H

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#define PRINT_RESULTS 0


static inline int32_t check_results_64(const char *isa_ase_name,
                                       const char *group_name,
                                       const char *instruction_name,
                                       const uint32_t test_count,
                                       const double elapsed_time,
                                       const uint64_t *b64_result,
                                       const uint64_t *b64_expect)
{
#if PRINT_RESULTS
    uint32_t ii;
    printf("\n");
    for (ii = 0; ii < test_count; ii++) {
        uint64_t a;
        memcpy(&a, (b64_result + ii), 8);
        if (ii % 8 != 0) {
            printf("        0x%016llxULL,\n", a);
        } else {
            printf("        0x%016llxULL,                    /* %3d  */\n",
                   a, ii);
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
        if (b64_result[i] == b64_expect[i]) {
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

typedef void (*binary_op_64) (const uint64_t*, const uint64_t*, uint64_t*);
static inline bool check_binary_op_64_with_input(binary_op_64 op, const char *op_str,
                                                 const uint64_t *expect,
                                                 const uint64_t *input,
                                                 size_t rows, size_t cols)
{
    bool ret = true;
    for (size_t i = 0; i < rows; i++) {
        for (size_t j = 0; j < cols; j++) {
            uint64_t input1 = input[i];
            uint64_t input2 = input[j];
            uint64_t expected = expect[i * cols + j];
            uint64_t res = 0;
            op(&input1, &input2, &res);
            printf("assert(0x%"PRIx64" == %s(0x%"PRIx64", 0x%"PRIx64"));\n",
                   res, op_str, input1, input2);
            if (expected != res) {
                printf("// error: expected 0x%"PRIx64"\n", expected);
                ret = false;
            }
        }
    }
    return ret;
}

static inline bool check_binary_op_64(binary_op_64 op, const char *op_str,
                                      const uint64_t *expect)
{
    bool res = true;
    size_t pattern_size = PATTERN_INPUTS_64_SHORT_COUNT;
    res &= check_binary_op_64_with_input(op, op_str,
                                         expect,
                                         b64_pattern, pattern_size, pattern_size);
    size_t random_size = RANDOM_INPUTS_64_SHORT_COUNT;
    res &= check_binary_op_64_with_input(op, op_str,
                                         expect + (pattern_size * pattern_size),
                                         b64_random, random_size, random_size);
    return res;
}

#endif
