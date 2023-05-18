/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Load/store for 128-bit atomic operations, AArch64 version.
 *
 * Copyright (C) 2018, 2023 Linaro, Ltd.
 *
 * See docs/devel/atomics.rst for discussion about the guarantees each
 * atomic primitive is meant to provide.
 */

#ifndef AARCH64_ATOMIC128_LDST_H
#define AARCH64_ATOMIC128_LDST_H

/* Through gcc 10, aarch64 has no support for 128-bit atomics.  */
#if !defined(CONFIG_ATOMIC128) && !defined(CONFIG_USER_ONLY)
/* We can do better than cmpxchg for AArch64.  */
static inline Int128 atomic16_read(Int128 *ptr)
{
    uint64_t l, h;
    uint32_t tmp;

    /* The load must be paired with the store to guarantee not tearing.  */
    asm("0: ldxp %[l], %[h], %[mem]\n\t"
        "stxp %w[tmp], %[l], %[h], %[mem]\n\t"
        "cbnz %w[tmp], 0b"
        : [mem] "+m"(*ptr), [tmp] "=r"(tmp), [l] "=r"(l), [h] "=r"(h));

    return int128_make128(l, h);
}

static inline void atomic16_set(Int128 *ptr, Int128 val)
{
    uint64_t l = int128_getlo(val), h = int128_gethi(val);
    uint64_t t1, t2;

    /* Load into temporaries to acquire the exclusive access lock.  */
    asm("0: ldxp %[t1], %[t2], %[mem]\n\t"
        "stxp %w[t1], %[l], %[h], %[mem]\n\t"
        "cbnz %w[t1], 0b"
        : [mem] "+m"(*ptr), [t1] "=&r"(t1), [t2] "=&r"(t2)
        : [l] "r"(l), [h] "r"(h));
}

# define HAVE_ATOMIC128 1
#else
#include "host/include/generic/host/atomic128-ldst.h"
#endif

#endif /* AARCH64_ATOMIC128_LDST_H */
