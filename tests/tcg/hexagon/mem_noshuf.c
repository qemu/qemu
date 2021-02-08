/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

/*
 *  Make sure that the :mem_noshuf packet attribute is honored.
 *  This is important when the addresses overlap.
 *  The store instruction in slot 1 effectively executes first,
 *  followed by the load instruction in slot 0.
 */

#define MEM_NOSHUF32(NAME, ST_TYPE, LD_TYPE, ST_OP, LD_OP) \
static inline unsigned int NAME(ST_TYPE * p, LD_TYPE * q, ST_TYPE x) \
{ \
    unsigned int ret; \
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
static inline unsigned long long NAME(ST_TYPE * p, LD_TYPE * q, ST_TYPE x) \
{ \
    unsigned long long ret; \
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
MEM_NOSHUF32(mem_noshuf_sb_lb,  signed char,  signed char,      memb, memb)
MEM_NOSHUF32(mem_noshuf_sb_lub, signed char,  unsigned char,    memb, memub)
MEM_NOSHUF32(mem_noshuf_sb_lh,  signed char,  signed short,     memb, memh)
MEM_NOSHUF32(mem_noshuf_sb_luh, signed char,  unsigned short,   memb, memuh)
MEM_NOSHUF32(mem_noshuf_sb_lw,  signed char,  signed int,       memb, memw)
MEM_NOSHUF64(mem_noshuf_sb_ld,  signed char,  signed long long, memb, memd)

/* Store half combinations */
MEM_NOSHUF32(mem_noshuf_sh_lb,  signed short, signed char,      memh, memb)
MEM_NOSHUF32(mem_noshuf_sh_lub, signed short, unsigned char,    memh, memub)
MEM_NOSHUF32(mem_noshuf_sh_lh,  signed short, signed short,     memh, memh)
MEM_NOSHUF32(mem_noshuf_sh_luh, signed short, unsigned short,   memh, memuh)
MEM_NOSHUF32(mem_noshuf_sh_lw,  signed short, signed int,       memh, memw)
MEM_NOSHUF64(mem_noshuf_sh_ld,  signed short, signed long long, memh, memd)

/* Store word combinations */
MEM_NOSHUF32(mem_noshuf_sw_lb,  signed int,   signed char,      memw, memb)
MEM_NOSHUF32(mem_noshuf_sw_lub, signed int,   unsigned char,    memw, memub)
MEM_NOSHUF32(mem_noshuf_sw_lh,  signed int,   signed short,     memw, memh)
MEM_NOSHUF32(mem_noshuf_sw_luh, signed int,   unsigned short,   memw, memuh)
MEM_NOSHUF32(mem_noshuf_sw_lw,  signed int,   signed int,       memw, memw)
MEM_NOSHUF64(mem_noshuf_sw_ld,  signed int,   signed long long, memw, memd)

/* Store double combinations */
MEM_NOSHUF32(mem_noshuf_sd_lb,  long long,    signed char,      memd, memb)
MEM_NOSHUF32(mem_noshuf_sd_lub, long long,    unsigned char,    memd, memub)
MEM_NOSHUF32(mem_noshuf_sd_lh,  long long,    signed short,     memd, memh)
MEM_NOSHUF32(mem_noshuf_sd_luh, long long,    unsigned short,   memd, memuh)
MEM_NOSHUF32(mem_noshuf_sd_lw,  long long,    signed int,       memd, memw)
MEM_NOSHUF64(mem_noshuf_sd_ld,  long long,    signed long long, memd, memd)

static inline unsigned int cancel_sw_lb(int pred, int *p, signed char *q, int x)
{
    unsigned int ret;
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

static inline
unsigned long long cancel_sw_ld(int pred, int *p, long long *q, int x)
{
    long long ret;
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
    signed long long d[2];
    unsigned long long ud[2];
    signed int w[4];
    unsigned int uw[4];
    signed short h[8];
    unsigned short uh[8];
    signed char b[16];
    unsigned char ub[16];
} Memory;

int err;

static void check32(int n, int expect)
{
    if (n != expect) {
        printf("ERROR: 0x%08x != 0x%08x\n", n, expect);
        err++;
    }
}

static void check64(long long n, long long expect)
{
    if (n != expect) {
        printf("ERROR: 0x%08llx != 0x%08llx\n", n, expect);
        err++;
    }
}

int main()
{
    Memory n;
    unsigned int res32;
    unsigned long long res64;

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
    res32 = cancel_sw_lb(0, &n.w[0], &n.b[0], 0x12345678);
    check32(res32, 0xffffffff);

    n.w[0] = ~0;
    res32 = cancel_sw_lb(1, &n.w[0], &n.b[0], 0x12345687);
    check32(res32, 0xffffff87);

    /*
     * Predicated double stores
     */
    n.d[0] = ~0LL;
    res64 = cancel_sw_ld(0, &n.w[0], &n.d[0], 0x12345678);
    check64(res64, 0xffffffffffffffffLL);

    n.d[0] = ~0LL;
    res64 = cancel_sw_ld(1, &n.w[0], &n.d[0], 0x12345678);
    check64(res64, 0xffffffff12345678LL);

    n.d[0] = ~0LL;
    res64 = cancel_sw_ld(0, &n.w[1], &n.d[0], 0x12345678);
    check64(res64, 0xffffffffffffffffLL);

    n.d[0] = ~0LL;
    res64 = cancel_sw_ld(1, &n.w[1], &n.d[0], 0x12345678);
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

    puts(err ? "FAIL" : "PASS");
    return err;
}
