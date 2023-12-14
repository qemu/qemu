/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Compare-and-swap for 128-bit atomic operations, generic version.
 *
 * Copyright (C) 2018, 2023 Linaro, Ltd.
 *
 * See docs/devel/atomics.rst for discussion about the guarantees each
 * atomic primitive is meant to provide.
 */

#ifndef HOST_ATOMIC128_CAS_H
#define HOST_ATOMIC128_CAS_H

#if defined(CONFIG_ATOMIC128)
static inline Int128 ATTRIBUTE_ATOMIC128_OPT
atomic16_cmpxchg(Int128 *ptr, Int128 cmp, Int128 new)
{
    __int128_t *ptr_align = __builtin_assume_aligned(ptr, 16);
    Int128Alias r, c, n;

    c.s = cmp;
    n.s = new;
    r.i = qatomic_cmpxchg__nocheck(ptr_align, c.i, n.i);
    return r.s;
}
# define HAVE_CMPXCHG128 1
#elif defined(CONFIG_CMPXCHG128)
static inline Int128 ATTRIBUTE_ATOMIC128_OPT
atomic16_cmpxchg(Int128 *ptr, Int128 cmp, Int128 new)
{
    Int128Aligned *ptr_align = __builtin_assume_aligned(ptr, 16);
    Int128Alias r, c, n;

    c.s = cmp;
    n.s = new;
    r.i = __sync_val_compare_and_swap_16(ptr_align, c.i, n.i);
    return r.s;
}
# define HAVE_CMPXCHG128 1
#else
/* Fallback definition that must be optimized away, or error.  */
Int128 QEMU_ERROR("unsupported atomic")
    atomic16_cmpxchg(Int128 *ptr, Int128 cmp, Int128 new);
# define HAVE_CMPXCHG128 0
#endif

#endif /* HOST_ATOMIC128_CAS_H */
