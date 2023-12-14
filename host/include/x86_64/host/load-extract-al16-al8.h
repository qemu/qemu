/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Atomic extract 64 from 128-bit, x86_64 version.
 *
 * Copyright (C) 2023 Linaro, Ltd.
 */

#ifndef X86_64_LOAD_EXTRACT_AL16_AL8_H
#define X86_64_LOAD_EXTRACT_AL16_AL8_H

#ifdef CONFIG_INT128_TYPE
#include "host/atomic128-ldst.h"

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
    __int128_t *ptr_align = (__int128_t *)(pi & ~7);
    int shr = (pi & 7) * 8;
    X86Int128Union r;

    /*
     * ptr_align % 16 is now only 0 or 8.
     * If the host supports atomic loads with VMOVDQU, then always use that,
     * making the branch highly predictable.  Otherwise we must use VMOVDQA
     * when ptr_align % 16 == 0 for 16-byte atomicity.
     */
    if ((cpuinfo & CPUINFO_ATOMIC_VMOVDQU) || (pi & 8)) {
        asm("vmovdqu %1, %0" : "=x" (r.v) : "m" (*ptr_align));
    } else {
        asm("vmovdqa %1, %0" : "=x" (r.v) : "m" (*ptr_align));
    }
    return int128_getlo(int128_urshift(r.s, shr));
}
#else
/* Fallback definition that must be optimized away, or error.  */
uint64_t QEMU_ERROR("unsupported atomic")
    load_atom_extract_al16_or_al8(void *pv, int s);
#endif

#endif /* X86_64_LOAD_EXTRACT_AL16_AL8_H */
