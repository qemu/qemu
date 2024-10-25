/*
 * Routines common to user and system emulation of load/store.
 *
 *  Copyright (c) 2022 Linaro, Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "host/load-extract-al16-al8.h.inc"
#include "host/store-insert-al16.h.inc"

#ifdef CONFIG_ATOMIC64
# define HAVE_al8          true
#else
# define HAVE_al8          false
#endif
#define HAVE_al8_fast      (ATOMIC_REG_SIZE >= 8)

/**
 * required_atomicity:
 *
 * Return the lg2 bytes of atomicity required by @memop for @p.
 * If the operation must be split into two operations to be
 * examined separately for atomicity, return -lg2.
 */
static int required_atomicity(CPUState *cpu, uintptr_t p, MemOp memop)
{
    MemOp atom = memop & MO_ATOM_MASK;
    MemOp size = memop & MO_SIZE;
    MemOp half = size ? size - 1 : 0;
    unsigned tmp;
    int atmax;

    switch (atom) {
    case MO_ATOM_NONE:
        atmax = MO_8;
        break;

    case MO_ATOM_IFALIGN_PAIR:
        size = half;
        /* fall through */

    case MO_ATOM_IFALIGN:
        tmp = (1 << size) - 1;
        atmax = p & tmp ? MO_8 : size;
        break;

    case MO_ATOM_WITHIN16:
        tmp = p & 15;
        atmax = (tmp + (1 << size) <= 16 ? size : MO_8);
        break;

    case MO_ATOM_WITHIN16_PAIR:
        tmp = p & 15;
        if (tmp + (1 << size) <= 16) {
            atmax = size;
        } else if (tmp + (1 << half) == 16) {
            /*
             * The pair exactly straddles the boundary.
             * Both halves are naturally aligned and atomic.
             */
            atmax = half;
        } else {
            /*
             * One of the pair crosses the boundary, and is non-atomic.
             * The other of the pair does not cross, and is atomic.
             */
            atmax = -half;
        }
        break;

    case MO_ATOM_SUBALIGN:
        /*
         * Examine the alignment of p to determine if there are subobjects
         * that must be aligned.  Note that we only really need ctz4() --
         * any more significant bits are discarded by the immediately
         * following comparison.
         */
        tmp = ctz32(p);
        atmax = MIN(size, tmp);
        break;

    default:
        g_assert_not_reached();
    }

    /*
     * Here we have the architectural atomicity of the operation.
     * However, when executing in a serial context, we need no extra
     * host atomicity in order to avoid racing.  This reduction
     * avoids looping with cpu_loop_exit_atomic.
     */
    if (cpu_in_serial_context(cpu)) {
        return MO_8;
    }
    return atmax;
}

/**
 * load_atomic2:
 * @pv: host address
 *
 * Atomically load 2 aligned bytes from @pv.
 */
static inline uint16_t load_atomic2(void *pv)
{
    uint16_t *p = __builtin_assume_aligned(pv, 2);
    return qatomic_read(p);
}

/**
 * load_atomic4:
 * @pv: host address
 *
 * Atomically load 4 aligned bytes from @pv.
 */
static inline uint32_t load_atomic4(void *pv)
{
    uint32_t *p = __builtin_assume_aligned(pv, 4);
    return qatomic_read(p);
}

/**
 * load_atomic8:
 * @pv: host address
 *
 * Atomically load 8 aligned bytes from @pv.
 */
static inline uint64_t load_atomic8(void *pv)
{
    uint64_t *p = __builtin_assume_aligned(pv, 8);

    qemu_build_assert(HAVE_al8);
    return qatomic_read__nocheck(p);
}

/**
 * load_atomic8_or_exit:
 * @cpu: generic cpu state
 * @ra: host unwind address
 * @pv: host address
 *
 * Atomically load 8 aligned bytes from @pv.
 * If this is not possible, longjmp out to restart serially.
 */
static uint64_t load_atomic8_or_exit(CPUState *cpu, uintptr_t ra, void *pv)
{
    if (HAVE_al8) {
        return load_atomic8(pv);
    }

#ifdef CONFIG_USER_ONLY
    /*
     * If the page is not writable, then assume the value is immutable
     * and requires no locking.  This ignores the case of MAP_SHARED with
     * another process, because the fallback start_exclusive solution
     * provides no protection across processes.
     */
    WITH_MMAP_LOCK_GUARD() {
        if (!page_check_range(h2g(pv), 8, PAGE_WRITE_ORG)) {
            uint64_t *p = __builtin_assume_aligned(pv, 8);
            return *p;
        }
    }
#endif

    /* Ultimate fallback: re-execute in serial context. */
    trace_load_atom8_or_exit_fallback(ra);
    cpu_loop_exit_atomic(cpu, ra);
}

/**
 * load_atomic16_or_exit:
 * @cpu: generic cpu state
 * @ra: host unwind address
 * @pv: host address
 *
 * Atomically load 16 aligned bytes from @pv.
 * If this is not possible, longjmp out to restart serially.
 */
static Int128 load_atomic16_or_exit(CPUState *cpu, uintptr_t ra, void *pv)
{
    Int128 *p = __builtin_assume_aligned(pv, 16);

    if (HAVE_ATOMIC128_RO) {
        return atomic16_read_ro(p);
    }

    /*
     * We can only use cmpxchg to emulate a load if the page is writable.
     * If the page is not writable, then assume the value is immutable
     * and requires no locking.  This ignores the case of MAP_SHARED with
     * another process, because the fallback start_exclusive solution
     * provides no protection across processes.
     *
     * In system mode all guest pages are writable.  For user mode,
     * we must take mmap_lock so that the query remains valid until
     * the write is complete -- tests/tcg/multiarch/munmap-pthread.c
     * is an example that can race.
     */
    WITH_MMAP_LOCK_GUARD() {
#ifdef CONFIG_USER_ONLY
        if (!page_check_range(h2g(p), 16, PAGE_WRITE_ORG)) {
            return *p;
        }
#endif
        if (HAVE_ATOMIC128_RW) {
            return atomic16_read_rw(p);
        }
    }

    /* Ultimate fallback: re-execute in serial context. */
    trace_load_atom16_or_exit_fallback(ra);
    cpu_loop_exit_atomic(cpu, ra);
}

/**
 * load_atom_extract_al4x2:
 * @pv: host address
 *
 * Load 4 bytes from @p, from two sequential atomic 4-byte loads.
 */
static uint32_t load_atom_extract_al4x2(void *pv)
{
    uintptr_t pi = (uintptr_t)pv;
    int sh = (pi & 3) * 8;
    uint32_t a, b;

    pv = (void *)(pi & ~3);
    a = load_atomic4(pv);
    b = load_atomic4(pv + 4);

    if (HOST_BIG_ENDIAN) {
        return (a << sh) | (b >> (-sh & 31));
    } else {
        return (a >> sh) | (b << (-sh & 31));
    }
}

/**
 * load_atom_extract_al8x2:
 * @pv: host address
 *
 * Load 8 bytes from @p, from two sequential atomic 8-byte loads.
 */
static uint64_t load_atom_extract_al8x2(void *pv)
{
    uintptr_t pi = (uintptr_t)pv;
    int sh = (pi & 7) * 8;
    uint64_t a, b;

    pv = (void *)(pi & ~7);
    a = load_atomic8(pv);
    b = load_atomic8(pv + 8);

    if (HOST_BIG_ENDIAN) {
        return (a << sh) | (b >> (-sh & 63));
    } else {
        return (a >> sh) | (b << (-sh & 63));
    }
}

/**
 * load_atom_extract_al8_or_exit:
 * @cpu: generic cpu state
 * @ra: host unwind address
 * @pv: host address
 * @s: object size in bytes, @s <= 4.
 *
 * Atomically load @s bytes from @p, when p % s != 0, and [p, p+s-1] does
 * not cross an 8-byte boundary.  This means that we can perform an atomic
 * 8-byte load and extract.
 * The value is returned in the low bits of a uint32_t.
 */
static uint32_t load_atom_extract_al8_or_exit(CPUState *cpu, uintptr_t ra,
                                              void *pv, int s)
{
    uintptr_t pi = (uintptr_t)pv;
    int o = pi & 7;
    int shr = (HOST_BIG_ENDIAN ? 8 - s - o : o) * 8;

    pv = (void *)(pi & ~7);
    return load_atomic8_or_exit(cpu, ra, pv) >> shr;
}

/**
 * load_atom_extract_al16_or_exit:
 * @cpu: generic cpu state
 * @ra: host unwind address
 * @p: host address
 * @s: object size in bytes, @s <= 8.
 *
 * Atomically load @s bytes from @p, when p % 16 < 8
 * and p % 16 + s > 8.  I.e. does not cross a 16-byte
 * boundary, but *does* cross an 8-byte boundary.
 * This is the slow version, so we must have eliminated
 * any faster load_atom_extract_al8_or_exit case.
 *
 * If this is not possible, longjmp out to restart serially.
 */
static uint64_t load_atom_extract_al16_or_exit(CPUState *cpu, uintptr_t ra,
                                               void *pv, int s)
{
    uintptr_t pi = (uintptr_t)pv;
    int o = pi & 7;
    int shr = (HOST_BIG_ENDIAN ? 16 - s - o : o) * 8;
    Int128 r;

    /*
     * Note constraints above: p & 8 must be clear.
     * Provoke SIGBUS if possible otherwise.
     */
    pv = (void *)(pi & ~7);
    r = load_atomic16_or_exit(cpu, ra, pv);

    r = int128_urshift(r, shr);
    return int128_getlo(r);
}

/**
 * load_atom_4_by_2:
 * @pv: host address
 *
 * Load 4 bytes from @pv, with two 2-byte atomic loads.
 */
static inline uint32_t load_atom_4_by_2(void *pv)
{
    uint32_t a = load_atomic2(pv);
    uint32_t b = load_atomic2(pv + 2);

    if (HOST_BIG_ENDIAN) {
        return (a << 16) | b;
    } else {
        return (b << 16) | a;
    }
}

/**
 * load_atom_8_by_2:
 * @pv: host address
 *
 * Load 8 bytes from @pv, with four 2-byte atomic loads.
 */
static inline uint64_t load_atom_8_by_2(void *pv)
{
    uint32_t a = load_atom_4_by_2(pv);
    uint32_t b = load_atom_4_by_2(pv + 4);

    if (HOST_BIG_ENDIAN) {
        return ((uint64_t)a << 32) | b;
    } else {
        return ((uint64_t)b << 32) | a;
    }
}

/**
 * load_atom_8_by_4:
 * @pv: host address
 *
 * Load 8 bytes from @pv, with two 4-byte atomic loads.
 */
static inline uint64_t load_atom_8_by_4(void *pv)
{
    uint32_t a = load_atomic4(pv);
    uint32_t b = load_atomic4(pv + 4);

    if (HOST_BIG_ENDIAN) {
        return ((uint64_t)a << 32) | b;
    } else {
        return ((uint64_t)b << 32) | a;
    }
}

/**
 * load_atom_8_by_8_or_4:
 * @pv: host address
 *
 * Load 8 bytes from aligned @pv, with at least 4-byte atomicity.
 */
static inline uint64_t load_atom_8_by_8_or_4(void *pv)
{
    if (HAVE_al8_fast) {
        return load_atomic8(pv);
    } else {
        return load_atom_8_by_4(pv);
    }
}

/**
 * load_atom_2:
 * @p: host address
 * @memop: the full memory op
 *
 * Load 2 bytes from @p, honoring the atomicity of @memop.
 */
static uint16_t load_atom_2(CPUState *cpu, uintptr_t ra,
                            void *pv, MemOp memop)
{
    uintptr_t pi = (uintptr_t)pv;
    int atmax;

    if (likely((pi & 1) == 0)) {
        return load_atomic2(pv);
    }
    if (HAVE_ATOMIC128_RO) {
        intptr_t left_in_page = -(pi | TARGET_PAGE_MASK);
        if (likely(left_in_page > 8)) {
            return load_atom_extract_al16_or_al8(pv, 2);
        }
    }

    atmax = required_atomicity(cpu, pi, memop);
    switch (atmax) {
    case MO_8:
        return lduw_he_p(pv);
    case MO_16:
        /* The only case remaining is MO_ATOM_WITHIN16. */
        if (!HAVE_al8_fast && (pi & 3) == 1) {
            /* Big or little endian, we want the middle two bytes. */
            return load_atomic4(pv - 1) >> 8;
        }
        if ((pi & 15) != 7) {
            return load_atom_extract_al8_or_exit(cpu, ra, pv, 2);
        }
        return load_atom_extract_al16_or_exit(cpu, ra, pv, 2);
    default:
        g_assert_not_reached();
    }
}

/**
 * load_atom_4:
 * @p: host address
 * @memop: the full memory op
 *
 * Load 4 bytes from @p, honoring the atomicity of @memop.
 */
static uint32_t load_atom_4(CPUState *cpu, uintptr_t ra,
                            void *pv, MemOp memop)
{
    uintptr_t pi = (uintptr_t)pv;
    int atmax;

    if (likely((pi & 3) == 0)) {
        return load_atomic4(pv);
    }
    if (HAVE_ATOMIC128_RO) {
        intptr_t left_in_page = -(pi | TARGET_PAGE_MASK);
        if (likely(left_in_page > 8)) {
            return load_atom_extract_al16_or_al8(pv, 4);
        }
    }

    atmax = required_atomicity(cpu, pi, memop);
    switch (atmax) {
    case MO_8:
    case MO_16:
    case -MO_16:
        /*
         * For MO_ATOM_IFALIGN, this is more atomicity than required,
         * but it's trivially supported on all hosts, better than 4
         * individual byte loads (when the host requires alignment),
         * and overlaps with the MO_ATOM_SUBALIGN case of p % 2 == 0.
         */
        return load_atom_extract_al4x2(pv);
    case MO_32:
        if (!(pi & 4)) {
            return load_atom_extract_al8_or_exit(cpu, ra, pv, 4);
        }
        return load_atom_extract_al16_or_exit(cpu, ra, pv, 4);
    default:
        g_assert_not_reached();
    }
}

/**
 * load_atom_8:
 * @p: host address
 * @memop: the full memory op
 *
 * Load 8 bytes from @p, honoring the atomicity of @memop.
 */
static uint64_t load_atom_8(CPUState *cpu, uintptr_t ra,
                            void *pv, MemOp memop)
{
    uintptr_t pi = (uintptr_t)pv;
    int atmax;

    /*
     * If the host does not support 8-byte atomics, wait until we have
     * examined the atomicity parameters below.
     */
    if (HAVE_al8 && likely((pi & 7) == 0)) {
        return load_atomic8(pv);
    }
    if (HAVE_ATOMIC128_RO) {
        return load_atom_extract_al16_or_al8(pv, 8);
    }

    atmax = required_atomicity(cpu, pi, memop);
    if (atmax == MO_64) {
        if (!HAVE_al8 && (pi & 7) == 0) {
            load_atomic8_or_exit(cpu, ra, pv);
        }
        return load_atom_extract_al16_or_exit(cpu, ra, pv, 8);
    }
    if (HAVE_al8_fast) {
        return load_atom_extract_al8x2(pv);
    }
    switch (atmax) {
    case MO_8:
        return ldq_he_p(pv);
    case MO_16:
        return load_atom_8_by_2(pv);
    case MO_32:
        return load_atom_8_by_4(pv);
    case -MO_32:
        if (HAVE_al8) {
            return load_atom_extract_al8x2(pv);
        }
        trace_load_atom8_fallback(memop, ra);
        cpu_loop_exit_atomic(cpu, ra);
    default:
        g_assert_not_reached();
    }
}

/**
 * load_atom_16:
 * @p: host address
 * @memop: the full memory op
 *
 * Load 16 bytes from @p, honoring the atomicity of @memop.
 */
static Int128 load_atom_16(CPUState *cpu, uintptr_t ra,
                           void *pv, MemOp memop)
{
    uintptr_t pi = (uintptr_t)pv;
    int atmax;
    Int128 r;
    uint64_t a, b;

    /*
     * If the host does not support 16-byte atomics, wait until we have
     * examined the atomicity parameters below.
     */
    if (HAVE_ATOMIC128_RO && likely((pi & 15) == 0)) {
        return atomic16_read_ro(pv);
    }

    atmax = required_atomicity(cpu, pi, memop);
    switch (atmax) {
    case MO_8:
        memcpy(&r, pv, 16);
        return r;
    case MO_16:
        a = load_atom_8_by_2(pv);
        b = load_atom_8_by_2(pv + 8);
        break;
    case MO_32:
        a = load_atom_8_by_4(pv);
        b = load_atom_8_by_4(pv + 8);
        break;
    case MO_64:
        if (!HAVE_al8) {
            trace_load_atom16_fallback(memop, ra);
            cpu_loop_exit_atomic(cpu, ra);
        }
        a = load_atomic8(pv);
        b = load_atomic8(pv + 8);
        break;
    case -MO_64:
        if (!HAVE_al8) {
            trace_load_atom16_fallback(memop, ra);
            cpu_loop_exit_atomic(cpu, ra);
        }
        a = load_atom_extract_al8x2(pv);
        b = load_atom_extract_al8x2(pv + 8);
        break;
    case MO_128:
        return load_atomic16_or_exit(cpu, ra, pv);
    default:
        g_assert_not_reached();
    }
    return int128_make128(HOST_BIG_ENDIAN ? b : a, HOST_BIG_ENDIAN ? a : b);
}

/**
 * store_atomic2:
 * @pv: host address
 * @val: value to store
 *
 * Atomically store 2 aligned bytes to @pv.
 */
static inline void store_atomic2(void *pv, uint16_t val)
{
    uint16_t *p = __builtin_assume_aligned(pv, 2);
    qatomic_set(p, val);
}

/**
 * store_atomic4:
 * @pv: host address
 * @val: value to store
 *
 * Atomically store 4 aligned bytes to @pv.
 */
static inline void store_atomic4(void *pv, uint32_t val)
{
    uint32_t *p = __builtin_assume_aligned(pv, 4);
    qatomic_set(p, val);
}

/**
 * store_atomic8:
 * @pv: host address
 * @val: value to store
 *
 * Atomically store 8 aligned bytes to @pv.
 */
static inline void store_atomic8(void *pv, uint64_t val)
{
    uint64_t *p = __builtin_assume_aligned(pv, 8);

    qemu_build_assert(HAVE_al8);
    qatomic_set__nocheck(p, val);
}

/**
 * store_atom_4x2
 */
static inline void store_atom_4_by_2(void *pv, uint32_t val)
{
    store_atomic2(pv, val >> (HOST_BIG_ENDIAN ? 16 : 0));
    store_atomic2(pv + 2, val >> (HOST_BIG_ENDIAN ? 0 : 16));
}

/**
 * store_atom_8_by_2
 */
static inline void store_atom_8_by_2(void *pv, uint64_t val)
{
    store_atom_4_by_2(pv, val >> (HOST_BIG_ENDIAN ? 32 : 0));
    store_atom_4_by_2(pv + 4, val >> (HOST_BIG_ENDIAN ? 0 : 32));
}

/**
 * store_atom_8_by_4
 */
static inline void store_atom_8_by_4(void *pv, uint64_t val)
{
    store_atomic4(pv, val >> (HOST_BIG_ENDIAN ? 32 : 0));
    store_atomic4(pv + 4, val >> (HOST_BIG_ENDIAN ? 0 : 32));
}

/**
 * store_atom_insert_al4:
 * @p: host address
 * @val: shifted value to store
 * @msk: mask for value to store
 *
 * Atomically store @val to @p, masked by @msk.
 */
static void store_atom_insert_al4(uint32_t *p, uint32_t val, uint32_t msk)
{
    uint32_t old, new;

    p = __builtin_assume_aligned(p, 4);
    old = qatomic_read(p);
    do {
        new = (old & ~msk) | val;
    } while (!__atomic_compare_exchange_n(p, &old, new, true,
                                          __ATOMIC_RELAXED, __ATOMIC_RELAXED));
}

/**
 * store_atom_insert_al8:
 * @p: host address
 * @val: shifted value to store
 * @msk: mask for value to store
 *
 * Atomically store @val to @p masked by @msk.
 */
static void store_atom_insert_al8(uint64_t *p, uint64_t val, uint64_t msk)
{
    uint64_t old, new;

    qemu_build_assert(HAVE_al8);
    p = __builtin_assume_aligned(p, 8);
    old = qatomic_read__nocheck(p);
    do {
        new = (old & ~msk) | val;
    } while (!__atomic_compare_exchange_n(p, &old, new, true,
                                          __ATOMIC_RELAXED, __ATOMIC_RELAXED));
}

/**
 * store_bytes_leN:
 * @pv: host address
 * @size: number of bytes to store
 * @val_le: data to store
 *
 * Store @size bytes at @p.  The bytes to store are extracted in little-endian order
 * from @val_le; return the bytes of @val_le beyond @size that have not been stored.
 */
static uint64_t store_bytes_leN(void *pv, int size, uint64_t val_le)
{
    uint8_t *p = pv;
    for (int i = 0; i < size; i++, val_le >>= 8) {
        p[i] = val_le;
    }
    return val_le;
}

/**
 * store_parts_leN
 * @pv: host address
 * @size: number of bytes to store
 * @val_le: data to store
 *
 * As store_bytes_leN, but atomically on each aligned part.
 */
G_GNUC_UNUSED
static uint64_t store_parts_leN(void *pv, int size, uint64_t val_le)
{
    do {
        int n;

        /* Find minimum of alignment and size */
        switch (((uintptr_t)pv | size) & 7) {
        case 4:
            store_atomic4(pv, le32_to_cpu(val_le));
            val_le >>= 32;
            n = 4;
            break;
        case 2:
        case 6:
            store_atomic2(pv, le16_to_cpu(val_le));
            val_le >>= 16;
            n = 2;
            break;
        default:
            *(uint8_t *)pv = val_le;
            val_le >>= 8;
            n = 1;
            break;
        case 0:
            g_assert_not_reached();
        }
        pv += n;
        size -= n;
    } while (size != 0);

    return val_le;
}

/**
 * store_whole_le4
 * @pv: host address
 * @size: number of bytes to store
 * @val_le: data to store
 *
 * As store_bytes_leN, but atomically as a whole.
 * Four aligned bytes are guaranteed to cover the store.
 */
static uint64_t store_whole_le4(void *pv, int size, uint64_t val_le)
{
    int sz = size * 8;
    int o = (uintptr_t)pv & 3;
    int sh = o * 8;
    uint32_t m = MAKE_64BIT_MASK(0, sz);
    uint32_t v;

    if (HOST_BIG_ENDIAN) {
        v = bswap32(val_le) >> sh;
        m = bswap32(m) >> sh;
    } else {
        v = val_le << sh;
        m <<= sh;
    }
    store_atom_insert_al4(pv - o, v, m);
    return val_le >> sz;
}

/**
 * store_whole_le8
 * @pv: host address
 * @size: number of bytes to store
 * @val_le: data to store
 *
 * As store_bytes_leN, but atomically as a whole.
 * Eight aligned bytes are guaranteed to cover the store.
 */
static uint64_t store_whole_le8(void *pv, int size, uint64_t val_le)
{
    int sz = size * 8;
    int o = (uintptr_t)pv & 7;
    int sh = o * 8;
    uint64_t m = MAKE_64BIT_MASK(0, sz);
    uint64_t v;

    qemu_build_assert(HAVE_al8);
    if (HOST_BIG_ENDIAN) {
        v = bswap64(val_le) >> sh;
        m = bswap64(m) >> sh;
    } else {
        v = val_le << sh;
        m <<= sh;
    }
    store_atom_insert_al8(pv - o, v, m);
    return val_le >> sz;
}

/**
 * store_whole_le16
 * @pv: host address
 * @size: number of bytes to store
 * @val_le: data to store
 *
 * As store_bytes_leN, but atomically as a whole.
 * 16 aligned bytes are guaranteed to cover the store.
 */
static uint64_t store_whole_le16(void *pv, int size, Int128 val_le)
{
    int sz = size * 8;
    int o = (uintptr_t)pv & 15;
    int sh = o * 8;
    Int128 m, v;

    qemu_build_assert(HAVE_CMPXCHG128);

    /* Like MAKE_64BIT_MASK(0, sz), but larger. */
    if (sz <= 64) {
        m = int128_make64(MAKE_64BIT_MASK(0, sz));
    } else {
        m = int128_make128(-1, MAKE_64BIT_MASK(0, sz - 64));
    }

    if (HOST_BIG_ENDIAN) {
        v = int128_urshift(bswap128(val_le), sh);
        m = int128_urshift(bswap128(m), sh);
    } else {
        v = int128_lshift(val_le, sh);
        m = int128_lshift(m, sh);
    }
    store_atom_insert_al16(pv - o, v, m);

    if (sz <= 64) {
        return 0;
    }
    return int128_gethi(val_le) >> (sz - 64);
}

/**
 * store_atom_2:
 * @p: host address
 * @val: the value to store
 * @memop: the full memory op
 *
 * Store 2 bytes to @p, honoring the atomicity of @memop.
 */
static void store_atom_2(CPUState *cpu, uintptr_t ra,
                         void *pv, MemOp memop, uint16_t val)
{
    uintptr_t pi = (uintptr_t)pv;
    int atmax;

    if (likely((pi & 1) == 0)) {
        store_atomic2(pv, val);
        return;
    }

    atmax = required_atomicity(cpu, pi, memop);
    if (atmax == MO_8) {
        stw_he_p(pv, val);
        return;
    }

    /*
     * The only case remaining is MO_ATOM_WITHIN16.
     * Big or little endian, we want the middle two bytes in each test.
     */
    if ((pi & 3) == 1) {
        store_atom_insert_al4(pv - 1, (uint32_t)val << 8, MAKE_64BIT_MASK(8, 16));
        return;
    } else if ((pi & 7) == 3) {
        if (HAVE_al8) {
            store_atom_insert_al8(pv - 3, (uint64_t)val << 24, MAKE_64BIT_MASK(24, 16));
            return;
        }
    } else if ((pi & 15) == 7) {
        if (HAVE_CMPXCHG128) {
            Int128 v = int128_lshift(int128_make64(val), 56);
            Int128 m = int128_lshift(int128_make64(0xffff), 56);
            store_atom_insert_al16(pv - 7, v, m);
            return;
        }
    } else {
        g_assert_not_reached();
    }

    trace_store_atom2_fallback(memop, ra);
    cpu_loop_exit_atomic(cpu, ra);
}

/**
 * store_atom_4:
 * @p: host address
 * @val: the value to store
 * @memop: the full memory op
 *
 * Store 4 bytes to @p, honoring the atomicity of @memop.
 */
static void store_atom_4(CPUState *cpu, uintptr_t ra,
                         void *pv, MemOp memop, uint32_t val)
{
    uintptr_t pi = (uintptr_t)pv;
    int atmax;

    if (likely((pi & 3) == 0)) {
        store_atomic4(pv, val);
        return;
    }

    atmax = required_atomicity(cpu, pi, memop);
    switch (atmax) {
    case MO_8:
        stl_he_p(pv, val);
        return;
    case MO_16:
        store_atom_4_by_2(pv, val);
        return;
    case -MO_16:
        {
            uint32_t val_le = cpu_to_le32(val);
            int s2 = pi & 3;
            int s1 = 4 - s2;

            switch (s2) {
            case 1:
                val_le = store_whole_le4(pv, s1, val_le);
                *(uint8_t *)(pv + 3) = val_le;
                break;
            case 3:
                *(uint8_t *)pv = val_le;
                store_whole_le4(pv + 1, s2, val_le >> 8);
                break;
            case 0: /* aligned */
            case 2: /* atmax MO_16 */
            default:
                g_assert_not_reached();
            }
        }
        return;
    case MO_32:
        if ((pi & 7) < 4) {
            if (HAVE_al8) {
                store_whole_le8(pv, 4, cpu_to_le32(val));
                return;
            }
        } else {
            if (HAVE_CMPXCHG128) {
                store_whole_le16(pv, 4, int128_make64(cpu_to_le32(val)));
                return;
            }
        }
        trace_store_atom4_fallback(memop, ra);
        cpu_loop_exit_atomic(cpu, ra);
    default:
        g_assert_not_reached();
    }
}

/**
 * store_atom_8:
 * @p: host address
 * @val: the value to store
 * @memop: the full memory op
 *
 * Store 8 bytes to @p, honoring the atomicity of @memop.
 */
static void store_atom_8(CPUState *cpu, uintptr_t ra,
                         void *pv, MemOp memop, uint64_t val)
{
    uintptr_t pi = (uintptr_t)pv;
    int atmax;

    if (HAVE_al8 && likely((pi & 7) == 0)) {
        store_atomic8(pv, val);
        return;
    }

    atmax = required_atomicity(cpu, pi, memop);
    switch (atmax) {
    case MO_8:
        stq_he_p(pv, val);
        return;
    case MO_16:
        store_atom_8_by_2(pv, val);
        return;
    case MO_32:
        store_atom_8_by_4(pv, val);
        return;
    case -MO_32:
        if (HAVE_al8) {
            uint64_t val_le = cpu_to_le64(val);
            int s2 = pi & 7;
            int s1 = 8 - s2;

            switch (s2) {
            case 1 ... 3:
                val_le = store_whole_le8(pv, s1, val_le);
                store_bytes_leN(pv + s1, s2, val_le);
                break;
            case 5 ... 7:
                val_le = store_bytes_leN(pv, s1, val_le);
                store_whole_le8(pv + s1, s2, val_le);
                break;
            case 0: /* aligned */
            case 4: /* atmax MO_32 */
            default:
                g_assert_not_reached();
            }
            return;
        }
        break;
    case MO_64:
        if (HAVE_CMPXCHG128) {
            store_whole_le16(pv, 8, int128_make64(cpu_to_le64(val)));
            return;
        }
        break;
    default:
        g_assert_not_reached();
    }
    trace_store_atom8_fallback(memop, ra);
    cpu_loop_exit_atomic(cpu, ra);
}

/**
 * store_atom_16:
 * @p: host address
 * @val: the value to store
 * @memop: the full memory op
 *
 * Store 16 bytes to @p, honoring the atomicity of @memop.
 */
static void store_atom_16(CPUState *cpu, uintptr_t ra,
                          void *pv, MemOp memop, Int128 val)
{
    uintptr_t pi = (uintptr_t)pv;
    uint64_t a, b;
    int atmax;

    if (HAVE_ATOMIC128_RW && likely((pi & 15) == 0)) {
        atomic16_set(pv, val);
        return;
    }

    atmax = required_atomicity(cpu, pi, memop);

    a = HOST_BIG_ENDIAN ? int128_gethi(val) : int128_getlo(val);
    b = HOST_BIG_ENDIAN ? int128_getlo(val) : int128_gethi(val);
    switch (atmax) {
    case MO_8:
        memcpy(pv, &val, 16);
        return;
    case MO_16:
        store_atom_8_by_2(pv, a);
        store_atom_8_by_2(pv + 8, b);
        return;
    case MO_32:
        store_atom_8_by_4(pv, a);
        store_atom_8_by_4(pv + 8, b);
        return;
    case MO_64:
        if (HAVE_al8) {
            store_atomic8(pv, a);
            store_atomic8(pv + 8, b);
            return;
        }
        break;
    case -MO_64:
        if (HAVE_CMPXCHG128) {
            uint64_t val_le;
            int s2 = pi & 15;
            int s1 = 16 - s2;

            if (HOST_BIG_ENDIAN) {
                val = bswap128(val);
            }
            switch (s2) {
            case 1 ... 7:
                val_le = store_whole_le16(pv, s1, val);
                store_bytes_leN(pv + s1, s2, val_le);
                break;
            case 9 ... 15:
                store_bytes_leN(pv, s1, int128_getlo(val));
                val = int128_urshift(val, s1 * 8);
                store_whole_le16(pv + s1, s2, val);
                break;
            case 0: /* aligned */
            case 8: /* atmax MO_64 */
            default:
                g_assert_not_reached();
            }
            return;
        }
        break;
    case MO_128:
        break;
    default:
        g_assert_not_reached();
    }
    trace_store_atom16_fallback(memop, ra);
    cpu_loop_exit_atomic(cpu, ra);
}
