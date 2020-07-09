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
#define HOST_UTILS_H

#include "qemu/bswap.h"

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

/* compute with 96 bit intermediate result: (a*b)/c */
static inline uint64_t muldiv64(uint64_t a, uint32_t b, uint32_t c)
{
    return (__int128_t)a * b / c;
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
void muls64(uint64_t *plow, uint64_t *phigh, int64_t a, int64_t b);
void mulu64(uint64_t *plow, uint64_t *phigh, uint64_t a, uint64_t b);
int divu128(uint64_t *plow, uint64_t *phigh, uint64_t divisor);
int divs128(int64_t *plow, int64_t *phigh, int64_t divisor);

static inline uint64_t muldiv64(uint64_t a, uint32_t b, uint32_t c)
{
    union {
        uint64_t ll;
        struct {
#ifdef HOST_WORDS_BIGENDIAN
            uint32_t high, low;
#else
            uint32_t low, high;
#endif
        } l;
    } u, res;
    uint64_t rl, rh;

    u.ll = a;
    rl = (uint64_t)u.l.low * (uint64_t)b;
    rh = (uint64_t)u.l.high * (uint64_t)b;
    rh += (rl >> 32);
    res.l.high = rh / c;
    res.l.low = (((rh % c) << 32) + (rl & 0xffffffff)) / c;
    return res.ll;
}
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
    return val ? __builtin_clz(val) : 32;
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
    return val ? __builtin_clzll(val) : 64;
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
    return val ? __builtin_ctz(val) : 32;
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
    return val ? __builtin_ctzll(val) : 64;
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
#if __has_builtin(__builtin_clrsb) || !defined(__clang__)
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
#if __has_builtin(__builtin_clrsbll) || !defined(__clang__)
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
    return __builtin_popcount(val);
}

/**
 * ctpop16 - count the population of one bits in a 16-bit value.
 * @val: The value to search
 */
static inline int ctpop16(uint16_t val)
{
    return __builtin_popcount(val);
}

/**
 * ctpop32 - count the population of one bits in a 32-bit value.
 * @val: The value to search
 */
static inline int ctpop32(uint32_t val)
{
    return __builtin_popcount(val);
}

/**
 * ctpop64 - count the population of one bits in a 64-bit value.
 * @val: The value to search
 */
static inline int ctpop64(uint64_t val)
{
    return __builtin_popcountll(val);
}

/**
 * revbit8 - reverse the bits in an 8-bit value.
 * @x: The value to modify.
 */
static inline uint8_t revbit8(uint8_t x)
{
    /* Assign the correct nibble position.  */
    x = ((x & 0xf0) >> 4)
      | ((x & 0x0f) << 4);
    /* Assign the correct bit position.  */
    x = ((x & 0x88) >> 3)
      | ((x & 0x44) >> 1)
      | ((x & 0x22) << 1)
      | ((x & 0x11) << 3);
    return x;
}

/**
 * revbit16 - reverse the bits in a 16-bit value.
 * @x: The value to modify.
 */
static inline uint16_t revbit16(uint16_t x)
{
    /* Assign the correct byte position.  */
    x = bswap16(x);
    /* Assign the correct nibble position.  */
    x = ((x & 0xf0f0) >> 4)
      | ((x & 0x0f0f) << 4);
    /* Assign the correct bit position.  */
    x = ((x & 0x8888) >> 3)
      | ((x & 0x4444) >> 1)
      | ((x & 0x2222) << 1)
      | ((x & 0x1111) << 3);
    return x;
}

/**
 * revbit32 - reverse the bits in a 32-bit value.
 * @x: The value to modify.
 */
static inline uint32_t revbit32(uint32_t x)
{
    /* Assign the correct byte position.  */
    x = bswap32(x);
    /* Assign the correct nibble position.  */
    x = ((x & 0xf0f0f0f0u) >> 4)
      | ((x & 0x0f0f0f0fu) << 4);
    /* Assign the correct bit position.  */
    x = ((x & 0x88888888u) >> 3)
      | ((x & 0x44444444u) >> 1)
      | ((x & 0x22222222u) << 1)
      | ((x & 0x11111111u) << 3);
    return x;
}

/**
 * revbit64 - reverse the bits in a 64-bit value.
 * @x: The value to modify.
 */
static inline uint64_t revbit64(uint64_t x)
{
    /* Assign the correct byte position.  */
    x = bswap64(x);
    /* Assign the correct nibble position.  */
    x = ((x & 0xf0f0f0f0f0f0f0f0ull) >> 4)
      | ((x & 0x0f0f0f0f0f0f0f0full) << 4);
    /* Assign the correct bit position.  */
    x = ((x & 0x8888888888888888ull) >> 3)
      | ((x & 0x4444444444444444ull) >> 1)
      | ((x & 0x2222222222222222ull) << 1)
      | ((x & 0x1111111111111111ull) << 3);
    return x;
}

/* Host type specific sizes of these routines.  */

#if ULONG_MAX == UINT32_MAX
# define clzl   clz32
# define ctzl   ctz32
# define clol   clo32
# define ctol   cto32
# define ctpopl ctpop32
# define revbitl revbit32
#elif ULONG_MAX == UINT64_MAX
# define clzl   clz64
# define ctzl   ctz64
# define clol   clo64
# define ctol   cto64
# define ctpopl ctpop64
# define revbitl revbit64
#else
# error Unknown sizeof long
#endif

static inline bool is_power_of_2(uint64_t value)
{
    if (!value) {
        return false;
    }

    return !(value & (value - 1));
}

/**
 * Return @value rounded down to the nearest power of two or zero.
 */
static inline uint64_t pow2floor(uint64_t value)
{
    if (!value) {
        /* Avoid undefined shift by 64 */
        return 0;
    }
    return 0x8000000000000000ull >> clz64(value);
}

/*
 * Return @value rounded up to the nearest power of two modulo 2^64.
 * This is *zero* for @value > 2^63, so be careful.
 */
static inline uint64_t pow2ceil(uint64_t value)
{
    int n = clz64(value - 1);

    if (!n) {
        /*
         * @value - 1 has no leading zeroes, thus @value - 1 >= 2^63
         * Therefore, either @value == 0 or @value > 2^63.
         * If it's 0, return 1, else return 0.
         */
        return !value;
    }
    return 0x8000000000000000ull >> (n - 1);
}

static inline uint32_t pow2roundup32(uint32_t x)
{
    x |= (x >> 1);
    x |= (x >> 2);
    x |= (x >> 4);
    x |= (x >> 8);
    x |= (x >> 16);
    return x + 1;
}

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
void urshift(uint64_t *plow, uint64_t *phigh, int32_t shift);

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
void ulshift(uint64_t *plow, uint64_t *phigh, int32_t shift, bool *overflow);

#endif
