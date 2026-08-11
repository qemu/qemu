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

/*
 * Test instructions where the semantics write to the destination
 * before all the operand reads have been completed.
 *
 * These instructions are problematic when we short-circuit the
 * register writes because the destination and source operands could
 * be the same TCGv.
 *
 * We test by forcing the read and write to be register r7.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

int err;

#include "hex_test.h"

#define insert(RES, X, WIDTH, OFFSET) \
    asm("r7 = %1\n\t" \
        "r7 = insert(r7, #" #WIDTH ", #" #OFFSET ")\n\t" \
        "%0 = r7\n\t" \
        : "=r"(RES) : "r"(X) : "r7")

static void test_insert(void)
{
    uint32_t res;

    insert(res, 0x12345678, 8, 1);
    check32(res, 0x123456f0);
    insert(res, 0x12345678, 0, 1);
    check32(res, 0x12345678);
    insert(res, 0x12345678, 20, 16);
    check32(res, 0x56785678);
}

static inline uint32_t insert_rp(uint32_t x, uint32_t width, uint32_t offset)
{
    uint64_t width_offset = (uint64_t)width << 32 | offset;
    uint32_t res;
    asm("r7 = %1\n\t"
        "r7 = insert(r7, %2)\n\t"
        "%0 = r7\n\t"
        : "=r"(res) : "r"(x), "r"(width_offset) : "r7");
    return res;

}

static void test_insert_rp(void)
{
    check32(insert_rp(0x12345678,   8,  1), 0x123456f0);
    check32(insert_rp(0x12345678,  63,  8), 0x34567878);
    check32(insert_rp(0x12345678, 127,  8), 0x34567878);
    check32(insert_rp(0x12345678,   8, 24), 0x78345678);
    check32(insert_rp(0x12345678,   8, 63), 0x12345678);
    check32(insert_rp(0x12345678,   8, 64), 0x00000000);
}

static inline uint32_t asr_r_svw_trun(uint64_t x, uint32_t y)
{
    uint32_t res;
    asm("r7 = %2\n\t"
        "r7 = vasrw(%1, r7)\n\t"
        "%0 = r7\n\t"
        : "=r"(res) : "r"(x), "r"(y) : "r7");
    return res;
}

static void test_asr_r_svw_trun(void)
{
    check32(asr_r_svw_trun(0x1111111122222222ULL, 5),
            0x88881111);
    check32(asr_r_svw_trun(0x1111111122222222ULL, 63),
            0x00000000);
    check32(asr_r_svw_trun(0x1111111122222222ULL, 64),
            0x00000000);
    check32(asr_r_svw_trun(0x1111111122222222ULL, 127),
            0x22224444);
    check32(asr_r_svw_trun(0x1111111122222222ULL, 128),
            0x11112222);
    check32(asr_r_svw_trun(0xffffffff22222222ULL, 128),
            0xffff2222);
}

static inline uint32_t swiz(uint32_t x)
{
    uint32_t res;
    asm("r7 = %1\n\t"
        "r7 = swiz(r7)\n\t"
        "%0 = r7\n\t"
        : "=r"(res) : "r"(x) : "r7");
    return res;
}

static void test_swiz(void)
{
    check32(swiz(0x11223344), 0x44332211);
}

#define CMPY(NAME, ASM) \
static inline uint32_t NAME##_rd_eq_rs(uint32_t x, uint32_t y) \
{ \
    uint32_t res; \
    asm("r7 = %1\n\t" \
        ASM("r7", "%2") "\n\t" \
        "%0 = r7\n\t" \
        : "=r"(res) : "r"(x), "r"(y) : "r7"); \
    return res; \
} \
static inline uint32_t NAME##_rd_eq_rt(uint32_t x, uint32_t y) \
{ \
    uint32_t res; \
    asm("r7 = %2\n\t" \
        ASM("%1", "r7") "\n\t" \
        "%0 = r7\n\t" \
        : "=r"(res) : "r"(x), "r"(y) : "r7"); \
    return res; \
}

#define CMPY_RND_SAT(RS, RT)     "r7 = cmpy(" RS "," RT "):rnd:sat"
#define CMPY_S1_RND_SAT(RS, RT)  "r7 = cmpy(" RS "," RT "):<<1:rnd:sat"
#define CMPYC_RND_SAT(RS, RT)    "r7 = cmpy(" RS "," RT "*):rnd:sat"
#define CMPYC_S1_RND_SAT(RS, RT) "r7 = cmpy(" RS "," RT "*):<<1:rnd:sat"

CMPY(cmpyrs_s0, CMPY_RND_SAT)
CMPY(cmpyrs_s1, CMPY_S1_RND_SAT)
CMPY(cmpyrsc_s0, CMPYC_RND_SAT)
CMPY(cmpyrsc_s1, CMPYC_S1_RND_SAT)

static void test_cmpy(void)
{
    check32(cmpyrs_s0_rd_eq_rs(0x32195ce2, 0xef862430), 0x011b105b);
    check32(cmpyrs_s0_rd_eq_rt(0x32195ce2, 0xef862430), 0x011b105b);
    check32(cmpyrs_s1_rd_eq_rs(0x32195ce2, 0xef862430), 0x023520b5);
    check32(cmpyrs_s1_rd_eq_rt(0x32195ce2, 0xef862430), 0x023520b5);
    check32(cmpyrsc_s0_rd_eq_rs(0x32195ce2, 0xef862430), 0x0d0f09e8);
    check32(cmpyrsc_s0_rd_eq_rt(0x32195ce2, 0xef862430), 0x0d0f09e8);
    check32(cmpyrsc_s1_rd_eq_rs(0x32195ce2, 0xef862430), 0x1a1f13d0);
    check32(cmpyrsc_s1_rd_eq_rt(0x32195ce2, 0xef862430), 0x1a1f13d0);

    /* Both halves saturate */
    check32(cmpyrs_s1_rd_eq_rs(0x80008000, 0x80008000), 0x7fff0000);
    check32(cmpyrsc_s1_rd_eq_rs(0x7fff8001, 0x80017fff), 0x00008000);
}

int main()
{
    test_insert();
    test_insert_rp();
    test_asr_r_svw_trun();
    test_swiz();
    test_cmpy();

    puts(err ? "FAIL" : "PASS");
    return err ? EXIT_FAILURE : EXIT_SUCCESS;
}
