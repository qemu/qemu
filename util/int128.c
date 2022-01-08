/*
 * 128-bit division and remainder for compilers not supporting __int128
 *
 * Copyright (c) 2021 Frédéric Pétrot <frederic.petrot@univ-grenoble-alpes.fr>
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
#include "qemu/int128.h"

#ifndef CONFIG_INT128

/*
 * Division and remainder algorithms for 128-bit due to Stefan Kanthak,
 * https://skanthak.homepage.t-online.de/integer.html#udivmodti4
 * Preconditions:
 *     - function should never be called with v equals to 0, it has to
 *       be dealt with beforehand
 *     - quotien pointer must be valid
 */
static Int128 divrem128(Int128 u, Int128 v, Int128 *q)
{
    Int128 qq;
    uint64_t hi, lo, tmp;
    int s = clz64(v.hi);

    if (s == 64) {
        /* we have uu÷0v => let's use divu128 */
        hi = u.hi;
        lo = u.lo;
        tmp = divu128(&lo, &hi, v.lo);
        *q = int128_make128(lo, hi);
        return int128_make128(tmp, 0);
    } else {
        hi = int128_gethi(int128_lshift(v, s));

        if (hi > u.hi) {
            lo = u.lo;
            tmp = u.hi;
            divu128(&lo, &tmp, hi);
            lo = int128_gethi(int128_lshift(int128_make128(lo, 0), s));
        } else { /* prevent overflow */
            lo = u.lo;
            tmp = u.hi - hi;
            divu128(&lo, &tmp, hi);
            lo = int128_gethi(int128_lshift(int128_make128(lo, 1), s));
        }

        qq = int128_make64(lo);

        tmp = lo * v.hi;
        mulu64(&lo, &hi, lo, v.lo);
        hi += tmp;

        if (hi < tmp     /* quotient * divisor >= 2**128 > dividend */
            || hi > u.hi /* quotient * divisor > dividend */
            || (hi == u.hi && lo > u.lo)) {
            qq.lo -= 1;
            mulu64(&lo, &hi, qq.lo, v.lo);
            hi += qq.lo * v.hi;
        }

        *q = qq;
        u.hi -= hi + (u.lo < lo);
        u.lo -= lo;
        return u;
    }
}

Int128 int128_divu(Int128 a, Int128 b)
{
    Int128 q;
    divrem128(a, b, &q);
    return q;
}

Int128 int128_remu(Int128 a, Int128 b)
{
    Int128 q;
    return divrem128(a, b, &q);
}

Int128 int128_divs(Int128 a, Int128 b)
{
    Int128 q;
    bool sgna = !int128_nonneg(a);
    bool sgnb = !int128_nonneg(b);

    if (sgna) {
        a = int128_neg(a);
    }

    if (sgnb) {
        b = int128_neg(b);
    }

    divrem128(a, b, &q);

    if (sgna != sgnb) {
        q = int128_neg(q);
    }

    return q;
}

Int128 int128_rems(Int128 a, Int128 b)
{
    Int128 q, r;
    bool sgna = !int128_nonneg(a);
    bool sgnb = !int128_nonneg(b);

    if (sgna) {
        a = int128_neg(a);
    }

    if (sgnb) {
        b = int128_neg(b);
    }

    r = divrem128(a, b, &q);

    if (sgna) {
        r = int128_neg(r);
    }

    return r;
}

#endif
