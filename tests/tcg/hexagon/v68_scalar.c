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
 *  Test the scalar core instructions that are new in v68
 */

int err;

static int buffer32[] = { 1, 2, 3, 4 };
static long long buffer64[] = { 5, 6, 7, 8 };

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

static inline int loadw_aq(int *p)
{
    int res;
    asm volatile("%0 = memw_aq(%1)\n\t"
                 : "=r"(res) : "r"(p));
    return res;
}

static void test_loadw_aq(void)
{
    int res;

    res = loadw_aq(&buffer32[0]);
    check32(res, 1);
    res = loadw_aq(&buffer32[1]);
    check32(res, 2);
}

static inline long long loadd_aq(long long *p)
{
    long long res;
    asm volatile("%0 = memd_aq(%1)\n\t"
                 : "=r"(res) : "r"(p));
    return res;
}

static void test_loadd_aq(void)
{
    long long res;

    res = loadd_aq(&buffer64[2]);
    check64(res, 7);
    res = loadd_aq(&buffer64[3]);
    check64(res, 8);
}

static inline void release_at(int *p)
{
    asm volatile("release(%0):at\n\t"
                 : : "r"(p));
}

static void test_release_at(void)
{
    release_at(&buffer32[2]);
    check64(buffer32[2], 3);
    release_at(&buffer32[3]);
    check64(buffer32[3], 4);
}

static inline void release_st(int *p)
{
    asm volatile("release(%0):st\n\t"
                 : : "r"(p));
}

static void test_release_st(void)
{
    release_st(&buffer32[2]);
    check64(buffer32[2], 3);
    release_st(&buffer32[3]);
    check64(buffer32[3], 4);
}

static inline void storew_rl_at(int *p, int val)
{
    asm volatile("memw_rl(%0):at = %1\n\t"
                 : : "r"(p), "r"(val) : "memory");
}

static void test_storew_rl_at(void)
{
    storew_rl_at(&buffer32[2], 9);
    check64(buffer32[2], 9);
    storew_rl_at(&buffer32[3], 10);
    check64(buffer32[3], 10);
}

static inline void stored_rl_at(long long *p, long long val)
{
    asm volatile("memd_rl(%0):at = %1\n\t"
                 : : "r"(p), "r"(val) : "memory");
}

static void test_stored_rl_at(void)
{
    stored_rl_at(&buffer64[2], 11);
    check64(buffer64[2], 11);
    stored_rl_at(&buffer64[3], 12);
    check64(buffer64[3], 12);
}

static inline void storew_rl_st(int *p, int val)
{
    asm volatile("memw_rl(%0):st = %1\n\t"
                 : : "r"(p), "r"(val) : "memory");
}

static void test_storew_rl_st(void)
{
    storew_rl_st(&buffer32[0], 13);
    check64(buffer32[0], 13);
    storew_rl_st(&buffer32[1], 14);
    check64(buffer32[1], 14);
}

static inline void stored_rl_st(long long *p, long long val)
{
    asm volatile("memd_rl(%0):st = %1\n\t"
                 : : "r"(p), "r"(val) : "memory");
}

static void test_stored_rl_st(void)
{
    stored_rl_st(&buffer64[0], 15);
    check64(buffer64[0], 15);
    stored_rl_st(&buffer64[1], 15);
    check64(buffer64[1], 15);
}

int main()
{
    test_loadw_aq();
    test_loadd_aq();
    test_release_at();
    test_release_st();
    test_storew_rl_at();
    test_stored_rl_at();
    test_storew_rl_st();
    test_stored_rl_st();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
