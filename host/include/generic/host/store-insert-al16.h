/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Atomic store insert into 128-bit, generic version.
 *
 * Copyright (C) 2023 Linaro, Ltd.
 */

#ifndef HOST_STORE_INSERT_AL16_H
#define HOST_STORE_INSERT_AL16_H

/**
 * store_atom_insert_al16:
 * @p: host address
 * @val: shifted value to store
 * @msk: mask for value to store
 *
 * Atomically store @val to @p masked by @msk.
 */
static inline void ATTRIBUTE_ATOMIC128_OPT
store_atom_insert_al16(Int128 *ps, Int128 val, Int128 msk)
{
#if defined(CONFIG_ATOMIC128)
    __uint128_t *pu;
    Int128Alias old, new;

    /* With CONFIG_ATOMIC128, we can avoid the memory barriers. */
    pu = __builtin_assume_aligned(ps, 16);
    old.u = *pu;
    msk = int128_not(msk);
    do {
        new.s = int128_and(old.s, msk);
        new.s = int128_or(new.s, val);
    } while (!__atomic_compare_exchange_n(pu, &old.u, new.u, true,
                                          __ATOMIC_RELAXED, __ATOMIC_RELAXED));
#else
    Int128 old, new, cmp;

    ps = __builtin_assume_aligned(ps, 16);
    old = *ps;
    msk = int128_not(msk);
    do {
        cmp = old;
        new = int128_and(old, msk);
        new = int128_or(new, val);
        old = atomic16_cmpxchg(ps, cmp, new);
    } while (int128_ne(cmp, old));
#endif
}

#endif /* HOST_STORE_INSERT_AL16_H */
