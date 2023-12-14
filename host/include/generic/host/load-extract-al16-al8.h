/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Atomic extract 64 from 128-bit, generic version.
 *
 * Copyright (C) 2023 Linaro, Ltd.
 */

#ifndef HOST_LOAD_EXTRACT_AL16_AL8_H
#define HOST_LOAD_EXTRACT_AL16_AL8_H

/**
 * load_atom_extract_al16_or_al8:
 * @pv: host address
 * @s: object size in bytes, @s <= 8.
 *
 * Load @s bytes from @pv, when pv % s != 0.  If [p, p+s-1] does not
 * cross an 16-byte boundary then the access must be 16-byte atomic,
 * otherwise the access must be 8-byte atomic.
 */
static inline uint64_t ATTRIBUTE_ATOMIC128_OPT
load_atom_extract_al16_or_al8(void *pv, int s)
{
    uintptr_t pi = (uintptr_t)pv;
    int o = pi & 7;
    int shr = (HOST_BIG_ENDIAN ? 16 - s - o : o) * 8;
    Int128 r;

    pv = (void *)(pi & ~7);
    if (pi & 8) {
        uint64_t *p8 = __builtin_assume_aligned(pv, 16, 8);
        uint64_t a = qatomic_read__nocheck(p8);
        uint64_t b = qatomic_read__nocheck(p8 + 1);

        if (HOST_BIG_ENDIAN) {
            r = int128_make128(b, a);
        } else {
            r = int128_make128(a, b);
        }
    } else {
        r = atomic16_read_ro(pv);
    }
    return int128_getlo(int128_urshift(r, shr));
}

#endif /* HOST_LOAD_EXTRACT_AL16_AL8_H */
