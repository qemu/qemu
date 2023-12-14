/*
 *  Copyright(c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
#include <stdbool.h>
#include <stdint.h>

/*
 *  Test the scalar core instructions that are new in v73
 */

int err;

static void __check32(int line, uint32_t result, uint32_t expect)
{
    if (result != expect) {
        printf("ERROR at line %d: 0x%08x != 0x%08x\n",
               line, result, expect);
        err++;
    }
}

#define check32(RES, EXP) __check32(__LINE__, RES, EXP)

static void __check64(int line, uint64_t result, uint64_t expect)
{
    if (result != expect) {
        printf("ERROR at line %d: 0x%016llx != 0x%016llx\n",
               line, result, expect);
        err++;
    }
}

#define check64(RES, EXP) __check64(__LINE__, RES, EXP)

static bool my_func_called;

static void my_func(void)
{
    my_func_called = true;
}

static inline void callrh(void *func)
{
    asm volatile("callrh %0\n\t"
                 : : "r"(func)
                 /* Mark the caller-save registers as clobbered */
                 : "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9",
                   "r10", "r11", "r12", "r13", "r14", "r15", "r28",
                   "p0", "p1", "p2", "p3");
}

static void test_callrh(void)
{
    my_func_called = false;
    callrh(&my_func);
    check32(my_func_called, true);
}

static void test_jumprh(void)
{
    uint32_t res;
    asm ("%0 = #5\n\t"
         "r0 = ##1f\n\t"
         "jumprh r0\n\t"
         "%0 = #3\n\t"
         "jump 2f\n\t"
         "1:\n\t"
         "%0 = #1\n\t"
         "2:\n\t"
         : "=r"(res) : : "r0");
    check32(res, 1);
}

int main()
{
    test_callrh();
    test_jumprh();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
