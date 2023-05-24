/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Atomic store insert into 128-bit, AArch64 version.
 *
 * Copyright (C) 2023 Linaro, Ltd.
 */

#ifndef AARCH64_STORE_INSERT_AL16_H
#define AARCH64_STORE_INSERT_AL16_H

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
    /*
     * GCC only implements __sync* primitives for int128 on aarch64.
     * We can do better without the barriers, and integrating the
     * arithmetic into the load-exclusive/store-conditional pair.
     */
    uint64_t tl, th, vl, vh, ml, mh;
    uint32_t fail;

    qemu_build_assert(!HOST_BIG_ENDIAN);
    vl = int128_getlo(val);
    vh = int128_gethi(val);
    ml = int128_getlo(msk);
    mh = int128_gethi(msk);

    asm("0: ldxp %[l], %[h], %[mem]\n\t"
        "bic %[l], %[l], %[ml]\n\t"
        "bic %[h], %[h], %[mh]\n\t"
        "orr %[l], %[l], %[vl]\n\t"
        "orr %[h], %[h], %[vh]\n\t"
        "stxp %w[f], %[l], %[h], %[mem]\n\t"
        "cbnz %w[f], 0b\n"
        : [mem] "+Q"(*ps), [f] "=&r"(fail), [l] "=&r"(tl), [h] "=&r"(th)
        : [vl] "r"(vl), [vh] "r"(vh), [ml] "r"(ml), [mh] "r"(mh));
}

#endif /* AARCH64_STORE_INSERT_AL16_H */
