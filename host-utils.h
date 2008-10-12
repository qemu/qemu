/*
 * Utility compute operations used by translated code.
 *
 * Copyright (c) 2007 Thiemo Seufer
 * Copyright (c) 2007 Jocelyn Mayer
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

#include "osdep.h"

#if defined(__x86_64__)
#define __HAVE_FAST_MULU64__
static always_inline void mulu64 (uint64_t *plow, uint64_t *phigh,
                                  uint64_t a, uint64_t b)
{
    __asm__ ("mul %0\n\t"
             : "=d" (*phigh), "=a" (*plow)
             : "a" (a), "0" (b));
}
#define __HAVE_FAST_MULS64__
static always_inline void muls64 (uint64_t *plow, uint64_t *phigh,
                                  int64_t a, int64_t b)
{
    __asm__ ("imul %0\n\t"
             : "=d" (*phigh), "=a" (*plow)
             : "a" (a), "0" (b));
}
#else
void muls64(uint64_t *phigh, uint64_t *plow, int64_t a, int64_t b);
void mulu64(uint64_t *phigh, uint64_t *plow, uint64_t a, uint64_t b);
#endif

/* Binary search for leading zeros.  */

static always_inline int clz32(uint32_t val)
{
#if defined(__GNUC__)
    if (val)
        return __builtin_clz(val);
    else
        return 32;
#else
    int cnt = 0;

    if (!(val & 0xFFFF0000U)) {
        cnt += 16;
        val <<= 16;
    }
    if (!(val & 0xFF000000U)) {
        cnt += 8;
        val <<= 8;
    }
    if (!(val & 0xF0000000U)) {
        cnt += 4;
        val <<= 4;
    }
    if (!(val & 0xC0000000U)) {
        cnt += 2;
        val <<= 2;
    }
    if (!(val & 0x80000000U)) {
        cnt++;
        val <<= 1;
    }
    if (!(val & 0x80000000U)) {
        cnt++;
    }
    return cnt;
#endif
}

static always_inline int clo32(uint32_t val)
{
    return clz32(~val);
}

static always_inline int clz64(uint64_t val)
{
#if defined(__GNUC__)
    if (val)
        return __builtin_clzll(val);
    else
        return 64;
#else
    int cnt = 0;

    if (!(val >> 32)) {
        cnt += 32;
    } else {
        val >>= 32;
    }

    return cnt + clz32(val);
#endif
}

static always_inline int clo64(uint64_t val)
{
    return clz64(~val);
}

static always_inline int ctz32 (uint32_t val)
{
#if defined(__GNUC__)
    if (val)
        return __builtin_ctz(val);
    else
        return 32;
#else
    int cnt;

    cnt = 0;
    if (!(val & 0x0000FFFFUL)) {
         cnt += 16;
        val >>= 16;
     }
    if (!(val & 0x000000FFUL)) {
         cnt += 8;
        val >>= 8;
     }
    if (!(val & 0x0000000FUL)) {
         cnt += 4;
        val >>= 4;
     }
    if (!(val & 0x00000003UL)) {
         cnt += 2;
        val >>= 2;
     }
    if (!(val & 0x00000001UL)) {
         cnt++;
        val >>= 1;
     }
    if (!(val & 0x00000001UL)) {
         cnt++;
     }

     return cnt;
#endif
 }
 
static always_inline int cto32 (uint32_t val)
 {
    return ctz32(~val);
}

static always_inline int ctz64 (uint64_t val)
{
#if defined(__GNUC__)
    if (val)
        return __builtin_ctz(val);
    else
        return 64;
#else
    int cnt;

    cnt = 0;
    if (!((uint32_t)val)) {
        cnt += 32;
        val >>= 32;
    }

    return cnt + ctz32(val);
#endif
}

static always_inline int cto64 (uint64_t val)
{
    return ctz64(~val);
}

static always_inline int ctpop8 (uint8_t val)
{
    val = (val & 0x55) + ((val >> 1) & 0x55);
    val = (val & 0x33) + ((val >> 2) & 0x33);
    val = (val & 0x0f) + ((val >> 4) & 0x0f);

    return val;
}

static always_inline int ctpop16 (uint16_t val)
{
    val = (val & 0x5555) + ((val >> 1) & 0x5555);
    val = (val & 0x3333) + ((val >> 2) & 0x3333);
    val = (val & 0x0f0f) + ((val >> 4) & 0x0f0f);
    val = (val & 0x00ff) + ((val >> 8) & 0x00ff);

    return val;
}

static always_inline int ctpop32 (uint32_t val)
{
#if defined(__GNUC__)
    return __builtin_popcount(val);
#else
    val = (val & 0x55555555) + ((val >>  1) & 0x55555555);
    val = (val & 0x33333333) + ((val >>  2) & 0x33333333);
    val = (val & 0x0f0f0f0f) + ((val >>  4) & 0x0f0f0f0f);
    val = (val & 0x00ff00ff) + ((val >>  8) & 0x00ff00ff);
    val = (val & 0x0000ffff) + ((val >> 16) & 0x0000ffff);

    return val;
#endif
}

static always_inline int ctpop64 (uint64_t val)
{
#if defined(__GNUC__)
    return __builtin_popcountll(val);
#else
    val = (val & 0x5555555555555555ULL) + ((val >>  1) & 0x5555555555555555ULL);
    val = (val & 0x3333333333333333ULL) + ((val >>  2) & 0x3333333333333333ULL);
    val = (val & 0x0f0f0f0f0f0f0f0fULL) + ((val >>  4) & 0x0f0f0f0f0f0f0f0fULL);
    val = (val & 0x00ff00ff00ff00ffULL) + ((val >>  8) & 0x00ff00ff00ff00ffULL);
    val = (val & 0x0000ffff0000ffffULL) + ((val >> 16) & 0x0000ffff0000ffffULL);
    val = (val & 0x00000000ffffffffULL) + ((val >> 32) & 0x00000000ffffffffULL);

    return val;
#endif
}
