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
#ifndef HOST_UTILS_H
#define HOST_UTILS_H 1

#include "qemu/compiler.h"   /* QEMU_GNUC_PREREQ */
#include <limits.h>

#ifdef CONFIG_INT128
static inline void mulu64(uint64_t *plow, uint64_t *phigh,
                          uint64_t a, uint64_t b)
{
    __uint128_t r = (__uint128_t)a * b;
    *plow = r;
    *phigh = r >> 64;
}

static inline void muls64(uint64_t *plow, uint64_t *phigh,
                          int64_t a, int64_t b)
{
    __int128_t r = (__int128_t)a * b;
    *plow = r;
    *phigh = r >> 64;
}

static inline int divu128(uint64_t *plow, uint64_t *phigh, uint64_t divisor)
{
    if (divisor == 0) {
        return 1;
    } else {
        __uint128_t dividend = ((__uint128_t)*phigh << 64) | *plow;
        __uint128_t result = dividend / divisor;
        *plow = result;
        *phigh = dividend % divisor;
        return result > UINT64_MAX;
    }
}

static inline int divs128(int64_t *plow, int64_t *phigh, int64_t divisor)
{
    if (divisor == 0) {
        return 1;
    } else {
        __int128_t dividend = ((__int128_t)*phigh << 64) | *plow;
        __int128_t result = dividend / divisor;
        *plow = result;
        *phigh = dividend % divisor;
        return result != *plow;
    }
}
#else
void muls64(uint64_t *phigh, uint64_t *plow, int64_t a, int64_t b);
void mulu64(uint64_t *phigh, uint64_t *plow, uint64_t a, uint64_t b);
int divu128(uint64_t *plow, uint64_t *phigh, uint64_t divisor);
int divs128(int64_t *plow, int64_t *phigh, int64_t divisor);
#endif

/**
 * clz32 - count leading zeros in a 32-bit value.
 * @val: The value to search
 *
 * Returns 32 if the value is zero.  Note that the GCC builtin is
 * undefined if the value is zero.
 */
static inline int clz32(uint32_t val)
{
#if QEMU_GNUC_PREREQ(3, 4)
    return val ? __builtin_clz(val) : 32;
#else
    /* Binary search for the leading one bit.  */
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

/**
 * clo32 - count leading ones in a 32-bit value.
 * @val: The value to search
 *
 * Returns 32 if the value is -1.
 */
static inline int clo32(uint32_t val)
{
    return clz32(~val);
}

/**
 * clz64 - count leading zeros in a 64-bit value.
 * @val: The value to search
 *
 * Returns 64 if the value is zero.  Note that the GCC builtin is
 * undefined if the value is zero.
 */
static inline int clz64(uint64_t val)
{
#if QEMU_GNUC_PREREQ(3, 4)
    return val ? __builtin_clzll(val) : 64;
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

/**
 * clo64 - count leading ones in a 64-bit value.
 * @val: The value to search
 *
 * Returns 64 if the value is -1.
 */
static inline int clo64(uint64_t val)
{
    return clz64(~val);
}

/**
 * ctz32 - count trailing zeros in a 32-bit value.
 * @val: The value to search
 *
 * Returns 32 if the value is zero.  Note that the GCC builtin is
 * undefined if the value is zero.
 */
static inline int ctz32(uint32_t val)
{
#if QEMU_GNUC_PREREQ(3, 4)
    return val ? __builtin_ctz(val) : 32;
#else
    /* Binary search for the trailing one bit.  */
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

/**
 * cto32 - count trailing ones in a 32-bit value.
 * @val: The value to search
 *
 * Returns 32 if the value is -1.
 */
static inline int cto32(uint32_t val)
{
    return ctz32(~val);
}

/**
 * ctz64 - count trailing zeros in a 64-bit value.
 * @val: The value to search
 *
 * Returns 64 if the value is zero.  Note that the GCC builtin is
 * undefined if the value is zero.
 */
static inline int ctz64(uint64_t val)
{
#if QEMU_GNUC_PREREQ(3, 4)
    return val ? __builtin_ctzll(val) : 64;
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

/**
 * cto64 - count trailing ones in a 64-bit value.
 * @val: The value to search
 *
 * Returns 64 if the value is -1.
 */
static inline int cto64(uint64_t val)
{
    return ctz64(~val);
}

/**
 * clrsb32 - count leading redundant sign bits in a 32-bit value.
 * @val: The value to search
 *
 * Returns the number of bits following the sign bit that are equal to it.
 * No special cases; output range is [0-31].
 */
static inline int clrsb32(uint32_t val)
{
#if QEMU_GNUC_PREREQ(4, 7)
    return __builtin_clrsb(val);
#else
    return clz32(val ^ ((int32_t)val >> 1)) - 1;
#endif
}

/**
 * clrsb64 - count leading redundant sign bits in a 64-bit value.
 * @val: The value to search
 *
 * Returns the number of bits following the sign bit that are equal to it.
 * No special cases; output range is [0-63].
 */
static inline int clrsb64(uint64_t val)
{
#if QEMU_GNUC_PREREQ(4, 7)
    return __builtin_clrsbll(val);
#else
    return clz64(val ^ ((int64_t)val >> 1)) - 1;
#endif
}

/**
 * ctpop8 - count the population of one bits in an 8-bit value.
 * @val: The value to search
 */
static inline int ctpop8(uint8_t val)
{
#if QEMU_GNUC_PREREQ(3, 4)
    return __builtin_popcount(val);
#else
    val = (val & 0x55) + ((val >> 1) & 0x55);
    val = (val & 0x33) + ((val >> 2) & 0x33);
    val = (val & 0x0f) + ((val >> 4) & 0x0f);

    return val;
#endif
}

/**
 * ctpop16 - count the population of one bits in a 16-bit value.
 * @val: The value to search
 */
static inline int ctpop16(uint16_t val)
{
#if QEMU_GNUC_PREREQ(3, 4)
    return __builtin_popcount(val);
#else
    val = (val & 0x5555) + ((val >> 1) & 0x5555);
    val = (val & 0x3333) + ((val >> 2) & 0x3333);
    val = (val & 0x0f0f) + ((val >> 4) & 0x0f0f);
    val = (val & 0x00ff) + ((val >> 8) & 0x00ff);

    return val;
#endif
}

/**
 * ctpop32 - count the population of one bits in a 32-bit value.
 * @val: The value to search
 */
static inline int ctpop32(uint32_t val)
{
#if QEMU_GNUC_PREREQ(3, 4)
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

/**
 * ctpop64 - count the population of one bits in a 64-bit value.
 * @val: The value to search
 */
static inline int ctpop64(uint64_t val)
{
#if QEMU_GNUC_PREREQ(3, 4)
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

/* Host type specific sizes of these routines.  */

#if ULONG_MAX == UINT32_MAX
# define clzl   clz32
# define ctzl   ctz32
# define clol   clo32
# define ctol   cto32
# define ctpopl ctpop32
#elif ULONG_MAX == UINT64_MAX
# define clzl   clz64
# define ctzl   ctz64
# define clol   clo64
# define ctol   cto64
# define ctpopl ctpop64
#else
# error Unknown sizeof long
#endif

#endif
