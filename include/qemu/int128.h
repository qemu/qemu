#ifndef INT128_H
#define INT128_H

#include "qemu/bswap.h"

/*
 * With TCI, we need to use libffi for interfacing with TCG helpers.
 * But libffi does not support __int128_t, and therefore cannot pass
 * or return values of this type, force use of the Int128 struct.
 */
#if defined(CONFIG_INT128) && !defined(CONFIG_TCG_INTERPRETER)
typedef __int128_t Int128;

static inline Int128 int128_make64(uint64_t a)
{
    return a;
}

static inline Int128 int128_makes64(int64_t a)
{
    return a;
}

static inline Int128 int128_make128(uint64_t lo, uint64_t hi)
{
    return (__uint128_t)hi << 64 | lo;
}

static inline uint64_t int128_get64(Int128 a)
{
    uint64_t r = a;
    assert(r == a);
    return r;
}

static inline uint64_t int128_getlo(Int128 a)
{
    return a;
}

static inline int64_t int128_gethi(Int128 a)
{
    return a >> 64;
}

static inline Int128 int128_zero(void)
{
    return 0;
}

static inline Int128 int128_one(void)
{
    return 1;
}

static inline Int128 int128_2_64(void)
{
    return (Int128)1 << 64;
}

static inline Int128 int128_exts64(int64_t a)
{
    return a;
}

static inline Int128 int128_not(Int128 a)
{
    return ~a;
}

static inline Int128 int128_and(Int128 a, Int128 b)
{
    return a & b;
}

static inline Int128 int128_or(Int128 a, Int128 b)
{
    return a | b;
}

static inline Int128 int128_xor(Int128 a, Int128 b)
{
    return a ^ b;
}

static inline Int128 int128_rshift(Int128 a, int n)
{
    return a >> n;
}

static inline Int128 int128_urshift(Int128 a, int n)
{
    return (__uint128_t)a >> n;
}

static inline Int128 int128_lshift(Int128 a, int n)
{
    return a << n;
}

static inline Int128 int128_add(Int128 a, Int128 b)
{
    return a + b;
}

static inline Int128 int128_neg(Int128 a)
{
    return -a;
}

static inline Int128 int128_sub(Int128 a, Int128 b)
{
    return a - b;
}

static inline bool int128_nonneg(Int128 a)
{
    return a >= 0;
}

static inline bool int128_eq(Int128 a, Int128 b)
{
    return a == b;
}

static inline bool int128_ne(Int128 a, Int128 b)
{
    return a != b;
}

static inline bool int128_ge(Int128 a, Int128 b)
{
    return a >= b;
}

static inline bool int128_uge(Int128 a, Int128 b)
{
    return ((__uint128_t)a) >= ((__uint128_t)b);
}

static inline bool int128_lt(Int128 a, Int128 b)
{
    return a < b;
}

static inline bool int128_ult(Int128 a, Int128 b)
{
    return (__uint128_t)a < (__uint128_t)b;
}

static inline bool int128_le(Int128 a, Int128 b)
{
    return a <= b;
}

static inline bool int128_gt(Int128 a, Int128 b)
{
    return a > b;
}

static inline bool int128_nz(Int128 a)
{
    return a != 0;
}

static inline Int128 int128_min(Int128 a, Int128 b)
{
    return a < b ? a : b;
}

static inline Int128 int128_max(Int128 a, Int128 b)
{
    return a > b ? a : b;
}

static inline void int128_addto(Int128 *a, Int128 b)
{
    *a += b;
}

static inline void int128_subfrom(Int128 *a, Int128 b)
{
    *a -= b;
}

static inline Int128 bswap128(Int128 a)
{
#if __has_builtin(__builtin_bswap128)
    return __builtin_bswap128(a);
#else
    return int128_make128(bswap64(int128_gethi(a)), bswap64(int128_getlo(a)));
#endif
}

static inline int clz128(Int128 a)
{
    if (a >> 64) {
        return __builtin_clzll(a >> 64);
    } else {
        return (a) ? __builtin_clzll((uint64_t)a) + 64 : 128;
    }
}

static inline Int128 int128_divu(Int128 a, Int128 b)
{
    return (__uint128_t)a / (__uint128_t)b;
}

static inline Int128 int128_remu(Int128 a, Int128 b)
{
    return (__uint128_t)a % (__uint128_t)b;
}

static inline Int128 int128_divs(Int128 a, Int128 b)
{
    return a / b;
}

static inline Int128 int128_rems(Int128 a, Int128 b)
{
    return a % b;
}

#else /* !CONFIG_INT128 */

typedef struct Int128 Int128;

/*
 * We guarantee that the in-memory byte representation of an
 * Int128 is that of a host-endian-order 128-bit integer
 * (whether using this struct or the __int128_t version of the type).
 * Some code using this type relies on this (eg when copying it into
 * guest memory or a gdb protocol buffer, or by using Int128 in
 * a union with other integer types).
 */
struct Int128 {
#if HOST_BIG_ENDIAN
    int64_t hi;
    uint64_t lo;
#else
    uint64_t lo;
    int64_t hi;
#endif
};

static inline Int128 int128_make64(uint64_t a)
{
    return (Int128) { .lo = a, .hi = 0 };
}

static inline Int128 int128_makes64(int64_t a)
{
    return (Int128) { .lo = a, .hi = a >> 63 };
}

static inline Int128 int128_make128(uint64_t lo, uint64_t hi)
{
    return (Int128) { .lo = lo, .hi = hi };
}

static inline uint64_t int128_get64(Int128 a)
{
    assert(!a.hi);
    return a.lo;
}

static inline uint64_t int128_getlo(Int128 a)
{
    return a.lo;
}

static inline int64_t int128_gethi(Int128 a)
{
    return a.hi;
}

static inline Int128 int128_zero(void)
{
    return int128_make64(0);
}

static inline Int128 int128_one(void)
{
    return int128_make64(1);
}

static inline Int128 int128_2_64(void)
{
    return int128_make128(0, 1);
}

static inline Int128 int128_exts64(int64_t a)
{
    return int128_make128(a, (a < 0) ? -1 : 0);
}

static inline Int128 int128_not(Int128 a)
{
    return int128_make128(~a.lo, ~a.hi);
}

static inline Int128 int128_and(Int128 a, Int128 b)
{
    return int128_make128(a.lo & b.lo, a.hi & b.hi);
}

static inline Int128 int128_or(Int128 a, Int128 b)
{
    return int128_make128(a.lo | b.lo, a.hi | b.hi);
}

static inline Int128 int128_xor(Int128 a, Int128 b)
{
    return int128_make128(a.lo ^ b.lo, a.hi ^ b.hi);
}

static inline Int128 int128_rshift(Int128 a, int n)
{
    int64_t h;
    if (!n) {
        return a;
    }
    h = a.hi >> (n & 63);
    if (n >= 64) {
        return int128_make128(h, h >> 63);
    } else {
        return int128_make128((a.lo >> n) | ((uint64_t)a.hi << (64 - n)), h);
    }
}

static inline Int128 int128_urshift(Int128 a, int n)
{
    uint64_t h = a.hi;
    if (!n) {
        return a;
    }
    h = h >> (n & 63);
    if (n >= 64) {
        return int128_make64(h);
    } else {
        return int128_make128((a.lo >> n) | ((uint64_t)a.hi << (64 - n)), h);
    }
}

static inline Int128 int128_lshift(Int128 a, int n)
{
    uint64_t l = a.lo << (n & 63);
    if (n >= 64) {
        return int128_make128(0, l);
    } else if (n > 0) {
        return int128_make128(l, (a.hi << n) | (a.lo >> (64 - n)));
    }
    return a;
}

static inline Int128 int128_add(Int128 a, Int128 b)
{
    uint64_t lo = a.lo + b.lo;

    /* a.lo <= a.lo + b.lo < a.lo + k (k is the base, 2^64).  Hence,
     * a.lo + b.lo >= k implies 0 <= lo = a.lo + b.lo - k < a.lo.
     * Similarly, a.lo + b.lo < k implies a.lo <= lo = a.lo + b.lo < k.
     *
     * So the carry is lo < a.lo.
     */
    return int128_make128(lo, (uint64_t)a.hi + b.hi + (lo < a.lo));
}

static inline Int128 int128_neg(Int128 a)
{
    uint64_t lo = -a.lo;
    return int128_make128(lo, ~(uint64_t)a.hi + !lo);
}

static inline Int128 int128_sub(Int128 a, Int128 b)
{
    return int128_make128(a.lo - b.lo, (uint64_t)a.hi - b.hi - (a.lo < b.lo));
}

static inline bool int128_nonneg(Int128 a)
{
    return a.hi >= 0;
}

static inline bool int128_eq(Int128 a, Int128 b)
{
    return a.lo == b.lo && a.hi == b.hi;
}

static inline bool int128_ne(Int128 a, Int128 b)
{
    return !int128_eq(a, b);
}

static inline bool int128_ge(Int128 a, Int128 b)
{
    return a.hi > b.hi || (a.hi == b.hi && a.lo >= b.lo);
}

static inline bool int128_uge(Int128 a, Int128 b)
{
    return (uint64_t)a.hi > (uint64_t)b.hi || (a.hi == b.hi && a.lo >= b.lo);
}

static inline bool int128_lt(Int128 a, Int128 b)
{
    return !int128_ge(a, b);
}

static inline bool int128_ult(Int128 a, Int128 b)
{
    return !int128_uge(a, b);
}

static inline bool int128_le(Int128 a, Int128 b)
{
    return int128_ge(b, a);
}

static inline bool int128_gt(Int128 a, Int128 b)
{
    return !int128_le(a, b);
}

static inline bool int128_nz(Int128 a)
{
    return a.lo || a.hi;
}

static inline Int128 int128_min(Int128 a, Int128 b)
{
    return int128_le(a, b) ? a : b;
}

static inline Int128 int128_max(Int128 a, Int128 b)
{
    return int128_ge(a, b) ? a : b;
}

static inline void int128_addto(Int128 *a, Int128 b)
{
    *a = int128_add(*a, b);
}

static inline void int128_subfrom(Int128 *a, Int128 b)
{
    *a = int128_sub(*a, b);
}

static inline Int128 bswap128(Int128 a)
{
    return int128_make128(bswap64(a.hi), bswap64(a.lo));
}

static inline int clz128(Int128 a)
{
    if (a.hi) {
        return __builtin_clzll(a.hi);
    } else {
        return (a.lo) ? __builtin_clzll(a.lo) + 64 : 128;
    }
}

Int128 int128_divu(Int128, Int128);
Int128 int128_remu(Int128, Int128);
Int128 int128_divs(Int128, Int128);
Int128 int128_rems(Int128, Int128);
#endif /* CONFIG_INT128 && !CONFIG_TCG_INTERPRETER */

static inline void bswap128s(Int128 *s)
{
    *s = bswap128(*s);
}

#define UINT128_MAX int128_make128(~0LL, ~0LL)
#define INT128_MAX int128_make128(UINT64_MAX, INT64_MAX)
#define INT128_MIN int128_make128(0, INT64_MIN)

/*
 * When compiler supports a 128-bit type, define a combination of
 * a possible structure and the native types.  Ease parameter passing
 * via use of the transparent union extension.
 */
#ifdef CONFIG_INT128
typedef union {
    Int128 s;
    __int128_t i;
    __uint128_t u;
} Int128Alias __attribute__((transparent_union));
#else
typedef Int128 Int128Alias;
#endif /* CONFIG_INT128 */

#endif /* INT128_H */
