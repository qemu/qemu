/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Compare-and-swap for 128-bit atomic operations, AArch64 version.
 *
 * Copyright (C) 2018, 2023 Linaro, Ltd.
 *
 * See docs/devel/atomics.rst for discussion about the guarantees each
 * atomic primitive is meant to provide.
 */

#ifndef AARCH64_ATOMIC128_CAS_H
#define AARCH64_ATOMIC128_CAS_H

/* Through gcc 10, aarch64 has no support for 128-bit atomics.  */
#if defined(CONFIG_ATOMIC128) || defined(CONFIG_CMPXCHG128)
#include "host/include/generic/host/atomic128-cas.h.inc"
#else
static inline Int128 atomic16_cmpxchg(Int128 *ptr, Int128 cmp, Int128 new)
{
    uint64_t cmpl = int128_getlo(cmp), cmph = int128_gethi(cmp);
    uint64_t newl = int128_getlo(new), newh = int128_gethi(new);
    uint64_t oldl, oldh;
    uint32_t tmp;

    asm("0: ldaxp %[oldl], %[oldh], %[mem]\n\t"
        "cmp %[oldl], %[cmpl]\n\t"
        "ccmp %[oldh], %[cmph], #0, eq\n\t"
        "b.ne 1f\n\t"
        "stlxp %w[tmp], %[newl], %[newh], %[mem]\n\t"
        "cbnz %w[tmp], 0b\n"
        "1:"
        : [mem] "+m"(*ptr), [tmp] "=&r"(tmp),
          [oldl] "=&r"(oldl), [oldh] "=&r"(oldh)
        : [cmpl] "r"(cmpl), [cmph] "r"(cmph),
          [newl] "r"(newl), [newh] "r"(newh)
        : "memory", "cc");

    return int128_make128(oldl, oldh);
}

# define CONFIG_CMPXCHG128 1
# define HAVE_CMPXCHG128 1
#endif

#endif /* AARCH64_ATOMIC128_CAS_H */
