/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Load/store for 128-bit atomic operations, x86_64 version.
 *
 * Copyright (C) 2023 Linaro, Ltd.
 *
 * See docs/devel/atomics.rst for discussion about the guarantees each
 * atomic primitive is meant to provide.
 */

#ifndef X86_64_ATOMIC128_LDST_H
#define X86_64_ATOMIC128_LDST_H

#ifdef CONFIG_INT128_TYPE
#include "host/cpuinfo.h"
#include "tcg/debug-assert.h"
#include <immintrin.h>

typedef union {
    __m128i v;
    __int128_t i;
    Int128 s;
} X86Int128Union;

/*
 * Through clang 16, with -mcx16, __atomic_load_n is incorrectly
 * expanded to a read-write operation: lock cmpxchg16b.
 */

#define HAVE_ATOMIC128_RO  likely(cpuinfo & CPUINFO_ATOMIC_VMOVDQA)
#define HAVE_ATOMIC128_RW  1

static inline Int128 atomic16_read_ro(const Int128 *ptr)
{
    X86Int128Union r;

    tcg_debug_assert(HAVE_ATOMIC128_RO);
    asm("vmovdqa %1, %0" : "=x" (r.v) : "m" (*ptr));

    return r.s;
}

static inline Int128 atomic16_read_rw(Int128 *ptr)
{
    __int128_t *ptr_align = __builtin_assume_aligned(ptr, 16);
    X86Int128Union r;

    if (HAVE_ATOMIC128_RO) {
        asm("vmovdqa %1, %0" : "=x" (r.v) : "m" (*ptr_align));
    } else {
        r.i = __sync_val_compare_and_swap_16(ptr_align, 0, 0);
    }
    return r.s;
}

static inline void atomic16_set(Int128 *ptr, Int128 val)
{
    __int128_t *ptr_align = __builtin_assume_aligned(ptr, 16);
    X86Int128Union new = { .s = val };

    if (HAVE_ATOMIC128_RO) {
        asm("vmovdqa %1, %0" : "=m"(*ptr_align) : "x" (new.v));
    } else {
        __int128_t old;
        do {
            old = *ptr_align;
        } while (!__sync_bool_compare_and_swap_16(ptr_align, old, new.i));
    }
}
#else
/* Provide QEMU_ERROR stubs. */
#include "host/include/generic/host/atomic128-ldst.h.inc"
#endif

#endif /* X86_64_ATOMIC128_LDST_H */
