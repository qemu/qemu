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
#ifdef HOST_WORDS_BIGENDIAN
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
