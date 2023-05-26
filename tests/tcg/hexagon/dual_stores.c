/*
 *  Copyright(c) 2019-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdint.h>

int err;

#include "hex_test.h"

/*
 *  Make sure that two stores in the same packet honor proper
 *  semantics: slot 1 executes first, then slot 0.
 *  This is important when the addresses overlap.
 */
static inline void dual_stores(int32_t *p, int8_t *q, int32_t x, int8_t y)
{
  asm volatile("{\n\t"
               "    memw(%0) = %2\n\t"
               "    memb(%1) = %3\n\t"
               "}\n"
               :: "r"(p), "r"(q), "r"(x), "r"(y)
               : "memory");
}

typedef union {
    int32_t word;
    int8_t byte;
} Dual;

int main()
{
    Dual d;

    d.word = ~0;
    dual_stores(&d.word, &d.byte, 0x12345678, 0xff);
    check32(d.word, 0x123456ff);

    puts(err ? "FAIL" : "PASS");
    return err;
}
