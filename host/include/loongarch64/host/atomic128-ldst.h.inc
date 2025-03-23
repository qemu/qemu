/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Load/store for 128-bit atomic operations, LoongArch version.
 *
 * See docs/devel/atomics.rst for discussion about the guarantees each
 * atomic primitive is meant to provide.
 */

#ifndef LOONGARCH_ATOMIC128_LDST_H
#define LOONGARCH_ATOMIC128_LDST_H

#include "host/cpuinfo.h"
#include "tcg/debug-assert.h"

#define HAVE_ATOMIC128_RO  likely(cpuinfo & CPUINFO_LSX)
#define HAVE_ATOMIC128_RW  HAVE_ATOMIC128_RO

/*
 * As of gcc 13 and clang 16, there is no compiler support for LSX at all.
 * Use inline assembly throughout.
 */

static inline Int128 atomic16_read_ro(const Int128 *ptr)
{
    uint64_t l, h;

    tcg_debug_assert(HAVE_ATOMIC128_RO);
    asm("vld $vr0, %2, 0\n\t"
        "vpickve2gr.d %0, $vr0, 0\n\t"
        "vpickve2gr.d %1, $vr0, 1"
        : "=r"(l), "=r"(h) : "r"(ptr), "m"(*ptr) : "$f0");

    return int128_make128(l, h);
}

static inline Int128 atomic16_read_rw(Int128 *ptr)
{
    return atomic16_read_ro(ptr);
}

static inline void atomic16_set(Int128 *ptr, Int128 val)
{
    uint64_t l = int128_getlo(val), h = int128_gethi(val);

    tcg_debug_assert(HAVE_ATOMIC128_RW);
    asm("vinsgr2vr.d $vr0, %1, 0\n\t"
        "vinsgr2vr.d $vr0, %2, 1\n\t"
        "vst $vr0, %3, 0"
        : "=m"(*ptr) : "r"(l), "r"(h), "r"(ptr) : "$f0");
}

#endif /* LOONGARCH_ATOMIC128_LDST_H */
