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
#include <stdbool.h>

int err;

#include "hex_test.h"

/*
 *  Make sure that the :mem_noshuf packet attribute is honored.
 *  This is important when the addresses overlap.
 *  The store instruction in slot 1 effectively executes first,
 *  followed by the load instruction in slot 0.
 */

#define MEM_NOSHUF32(NAME, ST_TYPE, LD_TYPE, ST_OP, LD_OP) \
static inline uint32_t NAME(ST_TYPE * p, LD_TYPE * q, ST_TYPE x) \
{ \
    uint32_t ret; \
    asm volatile("{\n\t" \
                 "    " #ST_OP "(%1) = %3\n\t" \
                 "    %0 = " #LD_OP "(%2)\n\t" \
                 "}:mem_noshuf\n" \
                 : "=r"(ret) \
                 : "r"(p), "r"(q), "r"(x) \
                 : "memory"); \
    return ret; \
}

#define MEM_NOSHUF64(NAME, ST_TYPE, LD_TYPE, ST_OP, LD_OP) \
static inline uint64_t NAME(ST_TYPE * p, LD_TYPE * q, ST_TYPE x) \
{ \
    uint64_t ret; \
    asm volatile("{\n\t" \
                 "    " #ST_OP "(%1) = %3\n\t" \
                 "    %0 = " #LD_OP "(%2)\n\t" \
                 "}:mem_noshuf\n" \
                 : "=r"(ret) \
                 : "r"(p), "r"(q), "r"(x) \
                 : "memory"); \
    return ret; \
}

/* Store byte combinations */
MEM_NOSHUF32(mem_noshuf_sb_lb,  int8_t,       int8_t,           memb, memb)
MEM_NOSHUF32(mem_noshuf_sb_lub, int8_t,       uint8_t,          memb, memub)
MEM_NOSHUF32(mem_noshuf_sb_lh,  int8_t,       int16_t,          memb, memh)
MEM_NOSHUF32(mem_noshuf_sb_luh, int8_t,       uint16_t,         memb, memuh)
MEM_NOSHUF32(mem_noshuf_sb_lw,  int8_t,       int32_t,          memb, memw)
MEM_NOSHUF64(mem_noshuf_sb_ld,  int8_t,       int64_t,          memb, memd)

/* Store half combinations */
MEM_NOSHUF32(mem_noshuf_sh_lb,  int16_t,      int8_t,           memh, memb)
MEM_NOSHUF32(mem_noshuf_sh_lub, int16_t,      uint8_t,          memh, memub)
MEM_NOSHUF32(mem_noshuf_sh_lh,  int16_t,      int16_t,          memh, memh)
MEM_NOSHUF32(mem_noshuf_sh_luh, int16_t,      uint16_t,         memh, memuh)
MEM_NOSHUF32(mem_noshuf_sh_lw,  int16_t,      int32_t,          memh, memw)
MEM_NOSHUF64(mem_noshuf_sh_ld,  int16_t,      int64_t,          memh, memd)

/* Store word combinations */
MEM_NOSHUF32(mem_noshuf_sw_lb,  int32_t,      int8_t,           memw, memb)
MEM_NOSHUF32(mem_noshuf_sw_lub, int32_t,      uint8_t,          memw, memub)
MEM_NOSHUF32(mem_noshuf_sw_lh,  int32_t,      int16_t,          memw, memh)
MEM_NOSHUF32(mem_noshuf_sw_luh, int32_t,      uint16_t,         memw, memuh)
MEM_NOSHUF32(mem_noshuf_sw_lw,  int32_t,      int32_t,          memw, memw)
MEM_NOSHUF64(mem_noshuf_sw_ld,  int32_t,      int64_t,          memw, memd)

/* Store double combinations */
MEM_NOSHUF32(mem_noshuf_sd_lb,  int64_t,      int8_t,           memd, memb)
MEM_NOSHUF32(mem_noshuf_sd_lub, int64_t,      uint8_t,          memd, memub)
MEM_NOSHUF32(mem_noshuf_sd_lh,  int64_t,      int16_t,          memd, memh)
MEM_NOSHUF32(mem_noshuf_sd_luh, int64_t,      uint16_t,         memd, memuh)
MEM_NOSHUF32(mem_noshuf_sd_lw,  int64_t,      int32_t,          memd, memw)
MEM_NOSHUF64(mem_noshuf_sd_ld,  int64_t,      int64_t,          memd, memd)

static inline int pred_lw_sw(bool pred, int32_t *p, int32_t *q,
                             int32_t x, int32_t y)
{
    int ret;
    asm volatile("p0 = cmp.eq(%5, #0)\n\t"
                 "%0 = %3\n\t"
                 "{\n\t"
                 "    memw(%1) = %4\n\t"
                 "    if (!p0) %0 = memw(%2)\n\t"
                 "}:mem_noshuf\n"
                 : "=&r"(ret)
                 : "r"(p), "r"(q), "r"(x), "r"(y), "r"(pred)
                 : "p0", "memory");
    return ret;
}

static inline int pred_lw_sw_pi(bool pred, int32_t *p, int32_t *q,
                                int32_t x, int32_t y)
{
    int ret;
    asm volatile("p0 = cmp.eq(%5, #0)\n\t"
                 "%0 = %3\n\t"
                 "r7 = %2\n\t"
                 "{\n\t"
                 "    memw(%1) = %4\n\t"
                 "    if (!p0) %0 = memw(r7++#4)\n\t"
                 "}:mem_noshuf\n"
                 : "=&r"(ret)
                 : "r"(p), "r"(q), "r"(x), "r"(y), "r"(pred)
                 : "r7", "p0", "memory");
    return ret;
}

static inline int64_t pred_ld_sd(bool pred, int64_t *p, int64_t *q,
                                 int64_t x, int64_t y)
{
    int64_t ret;
    asm volatile("p0 = cmp.eq(%5, #0)\n\t"
                 "%0 = %3\n\t"
                 "{\n\t"
                 "    memd(%1) = %4\n\t"
                 "    if (!p0) %0 = memd(%2)\n\t"
                 "}:mem_noshuf\n"
                 : "=&r"(ret)
                 : "r"(p), "r"(q), "r"(x), "r"(y), "r"(pred)
                 : "p0", "memory");
    return ret;
}

static inline int64_t pred_ld_sd_pi(bool pred, int64_t *p, int64_t *q,
                                    int64_t x, int64_t y)
{
    int64_t ret;
    asm volatile("p0 = cmp.eq(%5, #0)\n\t"
                 "%0 = %3\n\t"
                 "r7 = %2\n\t"
                 "{\n\t"
                 "    memd(%1) = %4\n\t"
                 "    if (!p0) %0 = memd(r7++#8)\n\t"
                 "}:mem_noshuf\n"
                 : "=&r"(ret)
                 : "r"(p), "r"(q), "r"(x), "r"(y), "r"(pred)
                 : "r7", "p0", "memory");
    return ret;
}

static inline int32_t cancel_sw_lb(bool pred, int32_t *p, int8_t *q, int32_t x)
{
    int32_t ret;
    asm volatile("p0 = cmp.eq(%4, #0)\n\t"
                 "{\n\t"
                 "    if (!p0) memw(%1) = %3\n\t"
                 "    %0 = memb(%2)\n\t"
                 "}:mem_noshuf\n"
                 : "=r"(ret)
                 : "r"(p), "r"(q), "r"(x), "r"(pred)
                 : "p0", "memory");
    return ret;
}

static inline int64_t cancel_sw_ld(bool pred, int32_t *p, int64_t *q, int32_t x)
{
    int64_t ret;
    asm volatile("p0 = cmp.eq(%4, #0)\n\t"
                 "{\n\t"
                 "    if (!p0) memw(%1) = %3\n\t"
                 "    %0 = memd(%2)\n\t"
                 "}:mem_noshuf\n"
                 : "=r"(ret)
                 : "r"(p), "r"(q), "r"(x), "r"(pred)
                 : "p0", "memory");
    return ret;
}

typedef union {
    int64_t d[2];
    uint64_t ud[2];
    int32_t w[4];
    uint32_t uw[4];
    int16_t h[8];
    uint16_t uh[8];
    int8_t b[16];
    uint8_t ub[16];
} Memory;

int main()
{
    Memory n;
    uint32_t res32;
    uint64_t res64;

    /*
     * Store byte combinations
     */
    n.w[0] = ~0;
    res32 = mem_noshuf_sb_lb(&n.b[0], &n.b[0], 0x87);
    check32(res32, 0xffffff87);

    n.w[0] = ~0;
    res32 = mem_noshuf_sb_lub(&n.b[0], &n.ub[0], 0x87);
    check32(res32, 0x00000087);

    n.w[0] = ~0;
    res32 = mem_noshuf_sb_lh(&n.b[0], &n.h[0], 0x87);
    check32(res32, 0xffffff87);

    n.w[0] = ~0;
    res32 = mem_noshuf_sb_luh(&n.b[0], &n.uh[0], 0x87);
    check32(res32, 0x0000ff87);

    n.w[0] = ~0;
    res32 = mem_noshuf_sb_lw(&n.b[0], &n.w[0], 0x87);
    check32(res32, 0xffffff87);

    n.d[0] = ~0LL;
    res64 = mem_noshuf_sb_ld(&n.b[0], &n.d[0], 0x87);
    check64(res64, 0xffffffffffffff87LL);

    /*
     * Store half combinations
     */
    n.w[0] = ~0;
    res32 = mem_noshuf_sh_lb(&n.h[0], &n.b[0], 0x8787);
    check32(res32, 0xffffff87);

    n.w[0] = ~0;
    res32 = mem_noshuf_sh_lub(&n.h[0], &n.ub[1], 0x8f87);
    check32(res32, 0x0000008f);

    n.w[0] = ~0;
    res32 = mem_noshuf_sh_lh(&n.h[0], &n.h[0], 0x8a87);
    check32(res32, 0xffff8a87);

    n.w[0] = ~0;
    res32 = mem_noshuf_sh_luh(&n.h[0], &n.uh[0], 0x8a87);
    check32(res32, 0x8a87);

    n.w[0] = ~0;
    res32 = mem_noshuf_sh_lw(&n.h[1], &n.w[0], 0x8a87);
    check32(res32, 0x8a87ffff);

    n.w[0] = ~0;
    res64 = mem_noshuf_sh_ld(&n.h[1], &n.d[0], 0x8a87);
    check64(res64, 0xffffffff8a87ffffLL);

    /*
     * Store word combinations
     */
    n.w[0] = ~0;
    res32 = mem_noshuf_sw_lb(&n.w[0], &n.b[0], 0x12345687);
    check32(res32, 0xffffff87);

    n.w[0] = ~0;
    res32 = mem_noshuf_sw_lub(&n.w[0], &n.ub[0], 0x12345687);
    check32(res32, 0x00000087);

    n.w[0] = ~0;
    res32 = mem_noshuf_sw_lh(&n.w[0], &n.h[0], 0x1234f678);
    check32(res32, 0xfffff678);

    n.w[0] = ~0;
    res32 = mem_noshuf_sw_luh(&n.w[0], &n.uh[0], 0x12345678);
    check32(res32, 0x00005678);

    n.w[0] = ~0;
    res32 = mem_noshuf_sw_lw(&n.w[0], &n.w[0], 0x12345678);
    check32(res32, 0x12345678);

    n.d[0] = ~0LL;
    res64 = mem_noshuf_sw_ld(&n.w[0], &n.d[0], 0x12345678);
    check64(res64, 0xffffffff12345678LL);

    /*
     * Store double combinations
     */
    n.d[0] = ~0LL;
    res32 = mem_noshuf_sd_lb(&n.d[0], &n.b[1], 0x123456789abcdef0);
    check32(res32, 0xffffffde);

    n.d[0] = ~0LL;
    res32 = mem_noshuf_sd_lub(&n.d[0], &n.ub[1], 0x123456789abcdef0);
    check32(res32, 0x000000de);

    n.d[0] = ~0LL;
    res32 = mem_noshuf_sd_lh(&n.d[0], &n.h[1], 0x123456789abcdef0);
    check32(res32, 0xffff9abc);

    n.d[0] = ~0LL;
    res32 = mem_noshuf_sd_luh(&n.d[0], &n.uh[1], 0x123456789abcdef0);
    check32(res32, 0x00009abc);

    n.d[0] = ~0LL;
    res32 = mem_noshuf_sd_lw(&n.d[0], &n.w[1], 0x123456789abcdef0);
    check32(res32, 0x12345678);

    n.d[0] = ~0LL;
    res64 = mem_noshuf_sd_ld(&n.d[0], &n.d[0], 0x123456789abcdef0);
    check64(res64, 0x123456789abcdef0LL);

    /*
     * Predicated word stores
     */
    n.w[0] = ~0;
    res32 = cancel_sw_lb(false, &n.w[0], &n.b[0], 0x12345678);
    check32(res32, 0xffffffff);

    n.w[0] = ~0;
    res32 = cancel_sw_lb(true, &n.w[0], &n.b[0], 0x12345687);
    check32(res32, 0xffffff87);

    /*
     * Predicated double stores
     */
    n.d[0] = ~0LL;
    res64 = cancel_sw_ld(false, &n.w[0], &n.d[0], 0x12345678);
    check64(res64, 0xffffffffffffffffLL);

    n.d[0] = ~0LL;
    res64 = cancel_sw_ld(true, &n.w[0], &n.d[0], 0x12345678);
    check64(res64, 0xffffffff12345678LL);

    n.d[0] = ~0LL;
    res64 = cancel_sw_ld(false, &n.w[1], &n.d[0], 0x12345678);
    check64(res64, 0xffffffffffffffffLL);

    n.d[0] = ~0LL;
    res64 = cancel_sw_ld(true, &n.w[1], &n.d[0], 0x12345678);
    check64(res64, 0x12345678ffffffffLL);

    /*
     * No overlap tests
     */
    n.w[0] = ~0;
    res32 = mem_noshuf_sb_lb(&n.b[1], &n.b[0], 0x87);
    check32(res32, 0xffffffff);

    n.w[0] = ~0;
    res32 = mem_noshuf_sb_lb(&n.b[0], &n.b[1], 0x87);
    check32(res32, 0xffffffff);

    n.w[0] = ~0;
    res32 = mem_noshuf_sh_lh(&n.h[1], &n.h[0], 0x8787);
    check32(res32, 0xffffffff);

    n.w[0] = ~0;
    res32 = mem_noshuf_sh_lh(&n.h[0], &n.h[1], 0x8787);
    check32(res32, 0xffffffff);

    n.d[0] = ~0LL;
    res32 = mem_noshuf_sw_lw(&n.w[0], &n.w[1], 0x12345678);
    check32(res32, 0xffffffff);

    n.d[0] = ~0LL;
    res32 = mem_noshuf_sw_lw(&n.w[1], &n.w[0], 0x12345678);
    check32(res32, 0xffffffff);

    n.d[0] = ~0LL;
    n.d[1] = ~0LL;
    res64 = mem_noshuf_sd_ld(&n.d[1], &n.d[0], 0x123456789abcdef0LL);
    check64(res64, 0xffffffffffffffffLL);

    n.d[0] = ~0LL;
    n.d[1] = ~0LL;
    res64 = mem_noshuf_sd_ld(&n.d[0], &n.d[1], 0x123456789abcdef0LL);
    check64(res64, 0xffffffffffffffffLL);

    n.w[0] = ~0;
    res32 = pred_lw_sw(false, &n.w[0], &n.w[0], 0x12345678, 0xc0ffeeda);
    check32(res32, 0x12345678);
    check32(n.w[0], 0xc0ffeeda);

    n.w[0] = ~0;
    res32 = pred_lw_sw(true, &n.w[0], &n.w[0], 0x12345678, 0xc0ffeeda);
    check32(res32, 0xc0ffeeda);
    check32(n.w[0], 0xc0ffeeda);

    n.w[0] = ~0;
    res32 = pred_lw_sw_pi(false, &n.w[0], &n.w[0], 0x12345678, 0xc0ffeeda);
    check32(res32, 0x12345678);
    check32(n.w[0], 0xc0ffeeda);

    n.w[0] = ~0;
    res32 = pred_lw_sw_pi(true, &n.w[0], &n.w[0], 0x12345678, 0xc0ffeeda);
    check32(res32, 0xc0ffeeda);
    check32(n.w[0], 0xc0ffeeda);

    n.d[0] = ~0LL;
    res64 = pred_ld_sd(false, &n.d[0], &n.d[0],
                       0x1234567812345678LL, 0xc0ffeedac0ffeedaLL);
    check64(res64, 0x1234567812345678LL);
    check64(n.d[0], 0xc0ffeedac0ffeedaLL);

    n.d[0] = ~0LL;
    res64 = pred_ld_sd(true, &n.d[0], &n.d[0],
                       0x1234567812345678LL, 0xc0ffeedac0ffeedaLL);
    check64(res64, 0xc0ffeedac0ffeedaLL);
    check64(n.d[0], 0xc0ffeedac0ffeedaLL);

    n.d[0] = ~0LL;
    res64 = pred_ld_sd_pi(false, &n.d[0], &n.d[0],
                          0x1234567812345678LL, 0xc0ffeedac0ffeedaLL);
    check64(res64, 0x1234567812345678LL);
    check64(n.d[0], 0xc0ffeedac0ffeedaLL);

    n.d[0] = ~0LL;
    res64 = pred_ld_sd_pi(true, &n.d[0], &n.d[0],
                          0x1234567812345678LL, 0xc0ffeedac0ffeedaLL);
    check64(res64, 0xc0ffeedac0ffeedaLL);
    check64(n.d[0], 0xc0ffeedac0ffeedaLL);

    puts(err ? "FAIL" : "PASS");
    return err;
}
