/*
 *  x86 integer helpers
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "cpu.h"
#include "host-utils.h"
#include "helper.h"

//#define DEBUG_MULDIV

/* modulo 9 table */
static const uint8_t rclb_table[32] = {
    0, 1, 2, 3, 4, 5, 6, 7,
    8, 0, 1, 2, 3, 4, 5, 6,
    7, 8, 0, 1, 2, 3, 4, 5,
    6, 7, 8, 0, 1, 2, 3, 4,
};

/* modulo 17 table */
static const uint8_t rclw_table[32] = {
    0, 1, 2, 3, 4, 5, 6, 7,
    8, 9, 10, 11, 12, 13, 14, 15,
    16, 0, 1, 2, 3, 4, 5, 6,
    7, 8, 9, 10, 11, 12, 13, 14,
};

/* division, flags are undefined */

void helper_divb_AL(CPUX86State *env, target_ulong t0)
{
    unsigned int num, den, q, r;

    num = (EAX & 0xffff);
    den = (t0 & 0xff);
    if (den == 0) {
        raise_exception(env, EXCP00_DIVZ);
    }
    q = (num / den);
    if (q > 0xff) {
        raise_exception(env, EXCP00_DIVZ);
    }
    q &= 0xff;
    r = (num % den) & 0xff;
    EAX = (EAX & ~0xffff) | (r << 8) | q;
}

void helper_idivb_AL(CPUX86State *env, target_ulong t0)
{
    int num, den, q, r;

    num = (int16_t)EAX;
    den = (int8_t)t0;
    if (den == 0) {
        raise_exception(env, EXCP00_DIVZ);
    }
    q = (num / den);
    if (q != (int8_t)q) {
        raise_exception(env, EXCP00_DIVZ);
    }
    q &= 0xff;
    r = (num % den) & 0xff;
    EAX = (EAX & ~0xffff) | (r << 8) | q;
}

void helper_divw_AX(CPUX86State *env, target_ulong t0)
{
    unsigned int num, den, q, r;

    num = (EAX & 0xffff) | ((EDX & 0xffff) << 16);
    den = (t0 & 0xffff);
    if (den == 0) {
        raise_exception(env, EXCP00_DIVZ);
    }
    q = (num / den);
    if (q > 0xffff) {
        raise_exception(env, EXCP00_DIVZ);
    }
    q &= 0xffff;
    r = (num % den) & 0xffff;
    EAX = (EAX & ~0xffff) | q;
    EDX = (EDX & ~0xffff) | r;
}

void helper_idivw_AX(CPUX86State *env, target_ulong t0)
{
    int num, den, q, r;

    num = (EAX & 0xffff) | ((EDX & 0xffff) << 16);
    den = (int16_t)t0;
    if (den == 0) {
        raise_exception(env, EXCP00_DIVZ);
    }
    q = (num / den);
    if (q != (int16_t)q) {
        raise_exception(env, EXCP00_DIVZ);
    }
    q &= 0xffff;
    r = (num % den) & 0xffff;
    EAX = (EAX & ~0xffff) | q;
    EDX = (EDX & ~0xffff) | r;
}

void helper_divl_EAX(CPUX86State *env, target_ulong t0)
{
    unsigned int den, r;
    uint64_t num, q;

    num = ((uint32_t)EAX) | ((uint64_t)((uint32_t)EDX) << 32);
    den = t0;
    if (den == 0) {
        raise_exception(env, EXCP00_DIVZ);
    }
    q = (num / den);
    r = (num % den);
    if (q > 0xffffffff) {
        raise_exception(env, EXCP00_DIVZ);
    }
    EAX = (uint32_t)q;
    EDX = (uint32_t)r;
}

void helper_idivl_EAX(CPUX86State *env, target_ulong t0)
{
    int den, r;
    int64_t num, q;

    num = ((uint32_t)EAX) | ((uint64_t)((uint32_t)EDX) << 32);
    den = t0;
    if (den == 0) {
        raise_exception(env, EXCP00_DIVZ);
    }
    q = (num / den);
    r = (num % den);
    if (q != (int32_t)q) {
        raise_exception(env, EXCP00_DIVZ);
    }
    EAX = (uint32_t)q;
    EDX = (uint32_t)r;
}

/* bcd */

/* XXX: exception */
void helper_aam(CPUX86State *env, int base)
{
    int al, ah;

    al = EAX & 0xff;
    ah = al / base;
    al = al % base;
    EAX = (EAX & ~0xffff) | al | (ah << 8);
    CC_DST = al;
}

void helper_aad(CPUX86State *env, int base)
{
    int al, ah;

    al = EAX & 0xff;
    ah = (EAX >> 8) & 0xff;
    al = ((ah * base) + al) & 0xff;
    EAX = (EAX & ~0xffff) | al;
    CC_DST = al;
}

void helper_aaa(CPUX86State *env)
{
    int icarry;
    int al, ah, af;
    int eflags;

    eflags = cpu_cc_compute_all(env, CC_OP);
    af = eflags & CC_A;
    al = EAX & 0xff;
    ah = (EAX >> 8) & 0xff;

    icarry = (al > 0xf9);
    if (((al & 0x0f) > 9) || af) {
        al = (al + 6) & 0x0f;
        ah = (ah + 1 + icarry) & 0xff;
        eflags |= CC_C | CC_A;
    } else {
        eflags &= ~(CC_C | CC_A);
        al &= 0x0f;
    }
    EAX = (EAX & ~0xffff) | al | (ah << 8);
    CC_SRC = eflags;
}

void helper_aas(CPUX86State *env)
{
    int icarry;
    int al, ah, af;
    int eflags;

    eflags = cpu_cc_compute_all(env, CC_OP);
    af = eflags & CC_A;
    al = EAX & 0xff;
    ah = (EAX >> 8) & 0xff;

    icarry = (al < 6);
    if (((al & 0x0f) > 9) || af) {
        al = (al - 6) & 0x0f;
        ah = (ah - 1 - icarry) & 0xff;
        eflags |= CC_C | CC_A;
    } else {
        eflags &= ~(CC_C | CC_A);
        al &= 0x0f;
    }
    EAX = (EAX & ~0xffff) | al | (ah << 8);
    CC_SRC = eflags;
}

void helper_daa(CPUX86State *env)
{
    int old_al, al, af, cf;
    int eflags;

    eflags = cpu_cc_compute_all(env, CC_OP);
    cf = eflags & CC_C;
    af = eflags & CC_A;
    old_al = al = EAX & 0xff;

    eflags = 0;
    if (((al & 0x0f) > 9) || af) {
        al = (al + 6) & 0xff;
        eflags |= CC_A;
    }
    if ((old_al > 0x99) || cf) {
        al = (al + 0x60) & 0xff;
        eflags |= CC_C;
    }
    EAX = (EAX & ~0xff) | al;
    /* well, speed is not an issue here, so we compute the flags by hand */
    eflags |= (al == 0) << 6; /* zf */
    eflags |= parity_table[al]; /* pf */
    eflags |= (al & 0x80); /* sf */
    CC_SRC = eflags;
}

void helper_das(CPUX86State *env)
{
    int al, al1, af, cf;
    int eflags;

    eflags = cpu_cc_compute_all(env, CC_OP);
    cf = eflags & CC_C;
    af = eflags & CC_A;
    al = EAX & 0xff;

    eflags = 0;
    al1 = al;
    if (((al & 0x0f) > 9) || af) {
        eflags |= CC_A;
        if (al < 6 || cf) {
            eflags |= CC_C;
        }
        al = (al - 6) & 0xff;
    }
    if ((al1 > 0x99) || cf) {
        al = (al - 0x60) & 0xff;
        eflags |= CC_C;
    }
    EAX = (EAX & ~0xff) | al;
    /* well, speed is not an issue here, so we compute the flags by hand */
    eflags |= (al == 0) << 6; /* zf */
    eflags |= parity_table[al]; /* pf */
    eflags |= (al & 0x80); /* sf */
    CC_SRC = eflags;
}

#ifdef TARGET_X86_64
static void add128(uint64_t *plow, uint64_t *phigh, uint64_t a, uint64_t b)
{
    *plow += a;
    /* carry test */
    if (*plow < a) {
        (*phigh)++;
    }
    *phigh += b;
}

static void neg128(uint64_t *plow, uint64_t *phigh)
{
    *plow = ~*plow;
    *phigh = ~*phigh;
    add128(plow, phigh, 1, 0);
}

/* return TRUE if overflow */
static int div64(uint64_t *plow, uint64_t *phigh, uint64_t b)
{
    uint64_t q, r, a1, a0;
    int i, qb, ab;

    a0 = *plow;
    a1 = *phigh;
    if (a1 == 0) {
        q = a0 / b;
        r = a0 % b;
        *plow = q;
        *phigh = r;
    } else {
        if (a1 >= b) {
            return 1;
        }
        /* XXX: use a better algorithm */
        for (i = 0; i < 64; i++) {
            ab = a1 >> 63;
            a1 = (a1 << 1) | (a0 >> 63);
            if (ab || a1 >= b) {
                a1 -= b;
                qb = 1;
            } else {
                qb = 0;
            }
            a0 = (a0 << 1) | qb;
        }
#if defined(DEBUG_MULDIV)
        printf("div: 0x%016" PRIx64 "%016" PRIx64 " / 0x%016" PRIx64
               ": q=0x%016" PRIx64 " r=0x%016" PRIx64 "\n",
               *phigh, *plow, b, a0, a1);
#endif
        *plow = a0;
        *phigh = a1;
    }
    return 0;
}

/* return TRUE if overflow */
static int idiv64(uint64_t *plow, uint64_t *phigh, int64_t b)
{
    int sa, sb;

    sa = ((int64_t)*phigh < 0);
    if (sa) {
        neg128(plow, phigh);
    }
    sb = (b < 0);
    if (sb) {
        b = -b;
    }
    if (div64(plow, phigh, b) != 0) {
        return 1;
    }
    if (sa ^ sb) {
        if (*plow > (1ULL << 63)) {
            return 1;
        }
        *plow = -*plow;
    } else {
        if (*plow >= (1ULL << 63)) {
            return 1;
        }
    }
    if (sa) {
        *phigh = -*phigh;
    }
    return 0;
}

void helper_mulq_EAX_T0(CPUX86State *env, target_ulong t0)
{
    uint64_t r0, r1;

    mulu64(&r0, &r1, EAX, t0);
    EAX = r0;
    EDX = r1;
    CC_DST = r0;
    CC_SRC = r1;
}

void helper_imulq_EAX_T0(CPUX86State *env, target_ulong t0)
{
    uint64_t r0, r1;

    muls64(&r0, &r1, EAX, t0);
    EAX = r0;
    EDX = r1;
    CC_DST = r0;
    CC_SRC = ((int64_t)r1 != ((int64_t)r0 >> 63));
}

target_ulong helper_imulq_T0_T1(CPUX86State *env, target_ulong t0,
                                target_ulong t1)
{
    uint64_t r0, r1;

    muls64(&r0, &r1, t0, t1);
    CC_DST = r0;
    CC_SRC = ((int64_t)r1 != ((int64_t)r0 >> 63));
    return r0;
}

void helper_divq_EAX(CPUX86State *env, target_ulong t0)
{
    uint64_t r0, r1;

    if (t0 == 0) {
        raise_exception(env, EXCP00_DIVZ);
    }
    r0 = EAX;
    r1 = EDX;
    if (div64(&r0, &r1, t0)) {
        raise_exception(env, EXCP00_DIVZ);
    }
    EAX = r0;
    EDX = r1;
}

void helper_idivq_EAX(CPUX86State *env, target_ulong t0)
{
    uint64_t r0, r1;

    if (t0 == 0) {
        raise_exception(env, EXCP00_DIVZ);
    }
    r0 = EAX;
    r1 = EDX;
    if (idiv64(&r0, &r1, t0)) {
        raise_exception(env, EXCP00_DIVZ);
    }
    EAX = r0;
    EDX = r1;
}
#endif

/* bit operations */
target_ulong helper_bsf(target_ulong t0)
{
    int count;
    target_ulong res;

    res = t0;
    count = 0;
    while ((res & 1) == 0) {
        count++;
        res >>= 1;
    }
    return count;
}

target_ulong helper_lzcnt(target_ulong t0, int wordsize)
{
    int count;
    target_ulong res, mask;

    if (wordsize > 0 && t0 == 0) {
        return wordsize;
    }
    res = t0;
    count = TARGET_LONG_BITS - 1;
    mask = (target_ulong)1 << (TARGET_LONG_BITS - 1);
    while ((res & mask) == 0) {
        count--;
        res <<= 1;
    }
    if (wordsize > 0) {
        return wordsize - 1 - count;
    }
    return count;
}

target_ulong helper_bsr(target_ulong t0)
{
    return helper_lzcnt(t0, 0);
}

#define SHIFT 0
#include "shift_helper_template.h"
#undef SHIFT

#define SHIFT 1
#include "shift_helper_template.h"
#undef SHIFT

#define SHIFT 2
#include "shift_helper_template.h"
#undef SHIFT

#ifdef TARGET_X86_64
#define SHIFT 3
#include "shift_helper_template.h"
#undef SHIFT
#endif
