/*
 * Utility compute operations used by translated code.
 *
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2007 Aurelien Jarno
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"

#ifndef CONFIG_INT128
/* Long integer helpers */
static inline void mul64(uint64_t *plow, uint64_t *phigh,
                         uint64_t a, uint64_t b)
{
    typedef union {
        uint64_t ll;
        struct {
#if HOST_BIG_ENDIAN
            uint32_t high, low;
#else
            uint32_t low, high;
#endif
        } l;
    } LL;
    LL rl, rm, rn, rh, a0, b0;
    uint64_t c;

    a0.ll = a;
    b0.ll = b;

    rl.ll = (uint64_t)a0.l.low * b0.l.low;
    rm.ll = (uint64_t)a0.l.low * b0.l.high;
    rn.ll = (uint64_t)a0.l.high * b0.l.low;
    rh.ll = (uint64_t)a0.l.high * b0.l.high;

    c = (uint64_t)rl.l.high + rm.l.low + rn.l.low;
    rl.l.high = c;
    c >>= 32;
    c = c + rm.l.high + rn.l.high + rh.l.low;
    rh.l.low = c;
    rh.l.high += (uint32_t)(c >> 32);

    *plow = rl.ll;
    *phigh = rh.ll;
}

/* Unsigned 64x64 -> 128 multiplication */
void mulu64 (uint64_t *plow, uint64_t *phigh, uint64_t a, uint64_t b)
{
    mul64(plow, phigh, a, b);
}

/* Signed 64x64 -> 128 multiplication */
void muls64 (uint64_t *plow, uint64_t *phigh, int64_t a, int64_t b)
{
    uint64_t rh;

    mul64(plow, &rh, a, b);

    /* Adjust for signs.  */
    if (b < 0) {
        rh -= a;
    }
    if (a < 0) {
        rh -= b;
    }
    *phigh = rh;
}

/*
 * Unsigned 128-by-64 division.
 * Returns the remainder.
 * Returns quotient via plow and phigh.
 * Also returns the remainder via the function return value.
 */
uint64_t divu128(uint64_t *plow, uint64_t *phigh, uint64_t divisor)
{
    uint64_t dhi = *phigh;
    uint64_t dlo = *plow;
    uint64_t rem, dhighest;
    int sh;

    if (divisor == 0 || dhi == 0) {
        *plow  = dlo / divisor;
        *phigh = 0;
        return dlo % divisor;
    } else {
        sh = clz64(divisor);

        if (dhi < divisor) {
            if (sh != 0) {
                /* normalize the divisor, shifting the dividend accordingly */
                divisor <<= sh;
                dhi = (dhi << sh) | (dlo >> (64 - sh));
                dlo <<= sh;
            }

            *phigh = 0;
            *plow = udiv_qrnnd(&rem, dhi, dlo, divisor);
        } else {
            if (sh != 0) {
                /* normalize the divisor, shifting the dividend accordingly */
                divisor <<= sh;
                dhighest = dhi >> (64 - sh);
                dhi = (dhi << sh) | (dlo >> (64 - sh));
                dlo <<= sh;

                *phigh = udiv_qrnnd(&dhi, dhighest, dhi, divisor);
            } else {
                /**
                 * dhi >= divisor
                 * Since the MSB of divisor is set (sh == 0),
                 * (dhi - divisor) < divisor
                 *
                 * Thus, the high part of the quotient is 1, and we can
                 * calculate the low part with a single call to udiv_qrnnd
                 * after subtracting divisor from dhi
                 */
                dhi -= divisor;
                *phigh = 1;
            }

            *plow = udiv_qrnnd(&rem, dhi, dlo, divisor);
        }

        /*
         * since the dividend/divisor might have been normalized,
         * the remainder might also have to be shifted back
         */
        return rem >> sh;
    }
}

/*
 * Signed 128-by-64 division.
 * Returns quotient via plow and phigh.
 * Also returns the remainder via the function return value.
 */
int64_t divs128(uint64_t *plow, int64_t *phigh, int64_t divisor)
{
    bool neg_quotient = false, neg_remainder = false;
    uint64_t unsig_hi = *phigh, unsig_lo = *plow;
    uint64_t rem;

    if (*phigh < 0) {
        neg_quotient = !neg_quotient;
        neg_remainder = !neg_remainder;

        if (unsig_lo == 0) {
            unsig_hi = -unsig_hi;
        } else {
            unsig_hi = ~unsig_hi;
            unsig_lo = -unsig_lo;
        }
    }

    if (divisor < 0) {
        neg_quotient = !neg_quotient;

        divisor = -divisor;
    }

    rem = divu128(&unsig_lo, &unsig_hi, (uint64_t)divisor);

    if (neg_quotient) {
        if (unsig_lo == 0) {
            *phigh = -unsig_hi;
            *plow = 0;
        } else {
            *phigh = ~unsig_hi;
            *plow = -unsig_lo;
        }
    } else {
        *phigh = unsig_hi;
        *plow = unsig_lo;
    }

    if (neg_remainder) {
        return -rem;
    } else {
        return rem;
    }
}
#endif

/**
 * urshift - 128-bit Unsigned Right Shift.
 * @plow: in/out - lower 64-bit integer.
 * @phigh: in/out - higher 64-bit integer.
 * @shift: in - bytes to shift, between 0 and 127.
 *
 * Result is zero-extended and stored in plow/phigh, which are
 * input/output variables. Shift values outside the range will
 * be mod to 128. In other words, the caller is responsible to
 * verify/assert both the shift range and plow/phigh pointers.
 */
void urshift(uint64_t *plow, uint64_t *phigh, int32_t shift)
{
    shift &= 127;
    if (shift == 0) {
        return;
    }

    uint64_t h = *phigh >> (shift & 63);
    if (shift >= 64) {
        *plow = h;
        *phigh = 0;
    } else {
        *plow = (*plow >> (shift & 63)) | (*phigh << (64 - (shift & 63)));
        *phigh = h;
    }
}

/**
 * ulshift - 128-bit Unsigned Left Shift.
 * @plow: in/out - lower 64-bit integer.
 * @phigh: in/out - higher 64-bit integer.
 * @shift: in - bytes to shift, between 0 and 127.
 * @overflow: out - true if any 1-bit is shifted out.
 *
 * Result is zero-extended and stored in plow/phigh, which are
 * input/output variables. Shift values outside the range will
 * be mod to 128. In other words, the caller is responsible to
 * verify/assert both the shift range and plow/phigh pointers.
 */
void ulshift(uint64_t *plow, uint64_t *phigh, int32_t shift, bool *overflow)
{
    uint64_t low = *plow;
    uint64_t high = *phigh;

    shift &= 127;
    if (shift == 0) {
        return;
    }

    /* check if any bit will be shifted out */
    urshift(&low, &high, 128 - shift);
    if (low | high) {
        *overflow = true;
    }

    if (shift >= 64) {
        *phigh = *plow << (shift & 63);
        *plow = 0;
    } else {
        *phigh = (*plow >> (64 - (shift & 63))) | (*phigh << (shift & 63));
        *plow = *plow << shift;
    }
}

/*
 * Unsigned 256-by-128 division.
 * Returns the remainder via r.
 * Returns lower 128 bit of quotient.
 * Needs a normalized divisor (most significant bit set to 1).
 *
 * Adapted from include/qemu/host-utils.h udiv_qrnnd,
 * from the GNU Multi Precision Library - longlong.h __udiv_qrnnd
 * (https://gmplib.org/repo/gmp/file/tip/longlong.h)
 *
 * Licensed under the GPLv2/LGPLv3
 */
static Int128 udiv256_qrnnd(Int128 *r, Int128 n1, Int128 n0, Int128 d)
{
    Int128 d0, d1, q0, q1, r1, r0, m;
    uint64_t mp0, mp1;

    d0 = int128_make64(int128_getlo(d));
    d1 = int128_make64(int128_gethi(d));

    r1 = int128_remu(n1, d1);
    q1 = int128_divu(n1, d1);
    mp0 = int128_getlo(q1);
    mp1 = int128_gethi(q1);
    mulu128(&mp0, &mp1, int128_getlo(d0));
    m = int128_make128(mp0, mp1);
    r1 = int128_make128(int128_gethi(n0), int128_getlo(r1));
    if (int128_ult(r1, m)) {
        q1 = int128_sub(q1, int128_one());
        r1 = int128_add(r1, d);
        if (int128_uge(r1, d)) {
            if (int128_ult(r1, m)) {
                q1 = int128_sub(q1, int128_one());
                r1 = int128_add(r1, d);
            }
        }
    }
    r1 = int128_sub(r1, m);

    r0 = int128_remu(r1, d1);
    q0 = int128_divu(r1, d1);
    mp0 = int128_getlo(q0);
    mp1 = int128_gethi(q0);
    mulu128(&mp0, &mp1, int128_getlo(d0));
    m = int128_make128(mp0, mp1);
    r0 = int128_make128(int128_getlo(n0), int128_getlo(r0));
    if (int128_ult(r0, m)) {
        q0 = int128_sub(q0, int128_one());
        r0 = int128_add(r0, d);
        if (int128_uge(r0, d)) {
            if (int128_ult(r0, m)) {
                q0 = int128_sub(q0, int128_one());
                r0 = int128_add(r0, d);
            }
        }
    }
    r0 = int128_sub(r0, m);

    *r = r0;
    return int128_or(int128_lshift(q1, 64), q0);
}

/*
 * Unsigned 256-by-128 division.
 * Returns the remainder.
 * Returns quotient via plow and phigh.
 * Also returns the remainder via the function return value.
 */
Int128 divu256(Int128 *plow, Int128 *phigh, Int128 divisor)
{
    Int128 dhi = *phigh;
    Int128 dlo = *plow;
    Int128 rem, dhighest;
    int sh;

    if (!int128_nz(divisor) || !int128_nz(dhi)) {
        *plow  = int128_divu(dlo, divisor);
        *phigh = int128_zero();
        return int128_remu(dlo, divisor);
    } else {
        sh = clz128(divisor);

        if (int128_ult(dhi, divisor)) {
            if (sh != 0) {
                /* normalize the divisor, shifting the dividend accordingly */
                divisor = int128_lshift(divisor, sh);
                dhi = int128_or(int128_lshift(dhi, sh),
                                int128_urshift(dlo, (128 - sh)));
                dlo = int128_lshift(dlo, sh);
            }

            *phigh = int128_zero();
            *plow = udiv256_qrnnd(&rem, dhi, dlo, divisor);
        } else {
            if (sh != 0) {
                /* normalize the divisor, shifting the dividend accordingly */
                divisor = int128_lshift(divisor, sh);
                dhighest = int128_rshift(dhi, (128 - sh));
                dhi = int128_or(int128_lshift(dhi, sh),
                                int128_urshift(dlo, (128 - sh)));
                dlo = int128_lshift(dlo, sh);

                *phigh = udiv256_qrnnd(&dhi, dhighest, dhi, divisor);
            } else {
                /*
                 * dhi >= divisor
                 * Since the MSB of divisor is set (sh == 0),
                 * (dhi - divisor) < divisor
                 *
                 * Thus, the high part of the quotient is 1, and we can
                 * calculate the low part with a single call to udiv_qrnnd
                 * after subtracting divisor from dhi
                 */
                dhi = int128_sub(dhi, divisor);
                *phigh = int128_one();
            }

            *plow = udiv256_qrnnd(&rem, dhi, dlo, divisor);
        }

        /*
         * since the dividend/divisor might have been normalized,
         * the remainder might also have to be shifted back
         */
        rem = int128_urshift(rem, sh);
        return rem;
    }
}

/*
 * Signed 256-by-128 division.
 * Returns quotient via plow and phigh.
 * Also returns the remainder via the function return value.
 */
Int128 divs256(Int128 *plow, Int128 *phigh, Int128 divisor)
{
    bool neg_quotient = false, neg_remainder = false;
    Int128 unsig_hi = *phigh, unsig_lo = *plow;
    Int128 rem;

    if (!int128_nonneg(*phigh)) {
        neg_quotient = !neg_quotient;
        neg_remainder = !neg_remainder;

        if (!int128_nz(unsig_lo)) {
            unsig_hi = int128_neg(unsig_hi);
        } else {
            unsig_hi = int128_not(unsig_hi);
            unsig_lo = int128_neg(unsig_lo);
        }
    }

    if (!int128_nonneg(divisor)) {
        neg_quotient = !neg_quotient;

        divisor = int128_neg(divisor);
    }

    rem = divu256(&unsig_lo, &unsig_hi, divisor);

    if (neg_quotient) {
        if (!int128_nz(unsig_lo)) {
            *phigh = int128_neg(unsig_hi);
            *plow = int128_zero();
        } else {
            *phigh = int128_not(unsig_hi);
            *plow = int128_neg(unsig_lo);
        }
    } else {
        *phigh = unsig_hi;
        *plow = unsig_lo;
    }

    if (neg_remainder) {
        return int128_neg(rem);
    } else {
        return rem;
    }
}
