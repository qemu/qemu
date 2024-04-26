/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Atomic extract 64 from 128-bit, AArch64 version.
 *
 * Copyright (C) 2023 Linaro, Ltd.
 */

#ifndef AARCH64_LOAD_EXTRACT_AL16_AL8_H
#define AARCH64_LOAD_EXTRACT_AL16_AL8_H

#include "host/cpuinfo.h"
#include "tcg/debug-assert.h"

/**
 * load_atom_extract_al16_or_al8:
 * @pv: host address
 * @s: object size in bytes, @s <= 8.
 *
 * Load @s bytes from @pv, when pv % s != 0.  If [p, p+s-1] does not
 * cross an 16-byte boundary then the access must be 16-byte atomic,
 * otherwise the access must be 8-byte atomic.
 */
static inline uint64_t load_atom_extract_al16_or_al8(void *pv, int s)
{
    uintptr_t pi = (uintptr_t)pv;
    __int128_t *ptr_align = (__int128_t *)(pi & ~7);
    int shr = (pi & 7) * 8;
    uint64_t l, h;

    /*
     * With FEAT_LSE2, LDP is single-copy atomic if 16-byte aligned
     * and single-copy atomic on the parts if 8-byte aligned.
     * All we need do is align the pointer mod 8.
     */
    tcg_debug_assert(HAVE_ATOMIC128_RO);
    asm("ldp %0, %1, %2" : "=r"(l), "=r"(h) : "m"(*ptr_align));
    return (l >> shr) | (h << (-shr & 63));
}

#endif /* AARCH64_LOAD_EXTRACT_AL16_AL8_H */
