/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Load/store for 128-bit atomic operations, generic version.
 *
 * Copyright (C) 2018, 2023 Linaro, Ltd.
 *
 * See docs/devel/atomics.rst for discussion about the guarantees each
 * atomic primitive is meant to provide.
 */

#ifndef HOST_ATOMIC128_LDST_H
#define HOST_ATOMIC128_LDST_H

#if defined(CONFIG_ATOMIC128)
static inline Int128 ATTRIBUTE_ATOMIC128_OPT
atomic16_read(Int128 *ptr)
{
    __int128_t *ptr_align = __builtin_assume_aligned(ptr, 16);
    Int128Alias r;

    r.i = qatomic_read__nocheck(ptr_align);
    return r.s;
}

static inline void ATTRIBUTE_ATOMIC128_OPT
atomic16_set(Int128 *ptr, Int128 val)
{
    __int128_t *ptr_align = __builtin_assume_aligned(ptr, 16);
    Int128Alias v;

    v.s = val;
    qatomic_set__nocheck(ptr_align, v.i);
}

# define HAVE_ATOMIC128 1
#elif !defined(CONFIG_USER_ONLY) && HAVE_CMPXCHG128
static inline Int128 ATTRIBUTE_ATOMIC128_OPT
atomic16_read(Int128 *ptr)
{
    /* Maybe replace 0 with 0, returning the old value.  */
    Int128 z = int128_make64(0);
    return atomic16_cmpxchg(ptr, z, z);
}

static inline void ATTRIBUTE_ATOMIC128_OPT
atomic16_set(Int128 *ptr, Int128 val)
{
    Int128 old = *ptr, cmp;
    do {
        cmp = old;
        old = atomic16_cmpxchg(ptr, cmp, val);
    } while (int128_ne(old, cmp));
}

# define HAVE_ATOMIC128 1
#else
/* Fallback definitions that must be optimized away, or error.  */
Int128 QEMU_ERROR("unsupported atomic") atomic16_read(Int128 *ptr);
void QEMU_ERROR("unsupported atomic") atomic16_set(Int128 *ptr, Int128 val);
# define HAVE_ATOMIC128 0
#endif

#endif /* HOST_ATOMIC128_LDST_H */
