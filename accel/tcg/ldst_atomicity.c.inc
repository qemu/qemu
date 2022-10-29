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

#ifdef CONFIG_ATOMIC64
# define HAVE_al8          true
#else
# define HAVE_al8          false
#endif
#define HAVE_al8_fast      (ATOMIC_REG_SIZE >= 8)

#if defined(CONFIG_ATOMIC128)
# define HAVE_al16_fast    true
#else
# define HAVE_al16_fast    false
#endif

/**
 * required_atomicity:
 *
 * Return the lg2 bytes of atomicity required by @memop for @p.
 * If the operation must be split into two operations to be
 * examined separately for atomicity, return -lg2.
 */
static int required_atomicity(CPUArchState *env, uintptr_t p, MemOp memop)
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
         * any more sigificant bits are discarded by the immediately
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
    if (cpu_in_serial_context(env_cpu(env))) {
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
 * load_atomic16:
 * @pv: host address
 *
 * Atomically load 16 aligned bytes from @pv.
 */
static inline Int128 load_atomic16(void *pv)
{
#ifdef CONFIG_ATOMIC128
    __uint128_t *p = __builtin_assume_aligned(pv, 16);
    Int128Alias r;

    r.u = qatomic_read__nocheck(p);
    return r.s;
#else
    qemu_build_not_reached();
#endif
}

/**
 * load_atomic8_or_exit:
 * @env: cpu context
 * @ra: host unwind address
 * @pv: host address
 *
 * Atomically load 8 aligned bytes from @pv.
 * If this is not possible, longjmp out to restart serially.
 */
static uint64_t load_atomic8_or_exit(CPUArchState *env, uintptr_t ra, void *pv)
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
    if (!page_check_range(h2g(pv), 8, PAGE_WRITE)) {
        uint64_t *p = __builtin_assume_aligned(pv, 8);
        return *p;
    }
#endif

    /* Ultimate fallback: re-execute in serial context. */
    cpu_loop_exit_atomic(env_cpu(env), ra);
}

/**
 * load_atomic16_or_exit:
 * @env: cpu context
 * @ra: host unwind address
 * @pv: host address
 *
 * Atomically load 16 aligned bytes from @pv.
 * If this is not possible, longjmp out to restart serially.
 */
static Int128 load_atomic16_or_exit(CPUArchState *env, uintptr_t ra, void *pv)
{
    Int128 *p = __builtin_assume_aligned(pv, 16);

    if (HAVE_al16_fast) {
        return load_atomic16(p);
    }

#ifdef CONFIG_USER_ONLY
    /*
     * We can only use cmpxchg to emulate a load if the page is writable.
     * If the page is not writable, then assume the value is immutable
     * and requires no locking.  This ignores the case of MAP_SHARED with
     * another process, because the fallback start_exclusive solution
     * provides no protection across processes.
     */
    if (!page_check_range(h2g(p), 16, PAGE_WRITE)) {
        return *p;
    }
#endif

    /*
     * In system mode all guest pages are writable, and for user-only
     * we have just checked writability.  Try cmpxchg.
     */
#if defined(CONFIG_CMPXCHG128)
    /* Swap 0 with 0, with the side-effect of returning the old value. */
    {
        Int128Alias r;
        r.u = __sync_val_compare_and_swap_16((__uint128_t *)p, 0, 0);
        return r.s;
    }
#endif

    /* Ultimate fallback: re-execute in serial context. */
    cpu_loop_exit_atomic(env_cpu(env), ra);
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
 * @env: cpu context
 * @ra: host unwind address
 * @pv: host address
 * @s: object size in bytes, @s <= 4.
 *
 * Atomically load @s bytes from @p, when p % s != 0, and [p, p+s-1] does
 * not cross an 8-byte boundary.  This means that we can perform an atomic
 * 8-byte load and extract.
 * The value is returned in the low bits of a uint32_t.
 */
static uint32_t load_atom_extract_al8_or_exit(CPUArchState *env, uintptr_t ra,
                                              void *pv, int s)
{
    uintptr_t pi = (uintptr_t)pv;
    int o = pi & 7;
    int shr = (HOST_BIG_ENDIAN ? 8 - s - o : o) * 8;

    pv = (void *)(pi & ~7);
    return load_atomic8_or_exit(env, ra, pv) >> shr;
}

/**
 * load_atom_extract_al16_or_exit:
 * @env: cpu context
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
static uint64_t load_atom_extract_al16_or_exit(CPUArchState *env, uintptr_t ra,
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
    r = load_atomic16_or_exit(env, ra, pv);

    r = int128_urshift(r, shr);
    return int128_getlo(r);
}

/**
 * load_atom_extract_al16_or_al8:
 * @p: host address
 * @s: object size in bytes, @s <= 8.
 *
 * Load @s bytes from @p, when p % s != 0.  If [p, p+s-1] does not
 * cross an 16-byte boundary then the access must be 16-byte atomic,
 * otherwise the access must be 8-byte atomic.
 */
static inline uint64_t load_atom_extract_al16_or_al8(void *pv, int s)
{
#if defined(CONFIG_ATOMIC128)
    uintptr_t pi = (uintptr_t)pv;
    int o = pi & 7;
    int shr = (HOST_BIG_ENDIAN ? 16 - s - o : o) * 8;
    __uint128_t r;

    pv = (void *)(pi & ~7);
    if (pi & 8) {
        uint64_t *p8 = __builtin_assume_aligned(pv, 16, 8);
        uint64_t a = qatomic_read__nocheck(p8);
        uint64_t b = qatomic_read__nocheck(p8 + 1);

        if (HOST_BIG_ENDIAN) {
            r = ((__uint128_t)a << 64) | b;
        } else {
            r = ((__uint128_t)b << 64) | a;
        }
    } else {
        __uint128_t *p16 = __builtin_assume_aligned(pv, 16, 0);
        r = qatomic_read__nocheck(p16);
    }
    return r >> shr;
#else
    qemu_build_not_reached();
#endif
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
 * load_atom_2:
 * @p: host address
 * @memop: the full memory op
 *
 * Load 2 bytes from @p, honoring the atomicity of @memop.
 */
static uint16_t load_atom_2(CPUArchState *env, uintptr_t ra,
                            void *pv, MemOp memop)
{
    uintptr_t pi = (uintptr_t)pv;
    int atmax;

    if (likely((pi & 1) == 0)) {
        return load_atomic2(pv);
    }
    if (HAVE_al16_fast) {
        return load_atom_extract_al16_or_al8(pv, 2);
    }

    atmax = required_atomicity(env, pi, memop);
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
            return load_atom_extract_al8_or_exit(env, ra, pv, 2);
        }
        return load_atom_extract_al16_or_exit(env, ra, pv, 2);
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
static uint32_t load_atom_4(CPUArchState *env, uintptr_t ra,
                            void *pv, MemOp memop)
{
    uintptr_t pi = (uintptr_t)pv;
    int atmax;

    if (likely((pi & 3) == 0)) {
        return load_atomic4(pv);
    }
    if (HAVE_al16_fast) {
        return load_atom_extract_al16_or_al8(pv, 4);
    }

    atmax = required_atomicity(env, pi, memop);
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
            return load_atom_extract_al8_or_exit(env, ra, pv, 4);
        }
        return load_atom_extract_al16_or_exit(env, ra, pv, 4);
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
static uint64_t load_atom_8(CPUArchState *env, uintptr_t ra,
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
    if (HAVE_al16_fast) {
        return load_atom_extract_al16_or_al8(pv, 8);
    }

    atmax = required_atomicity(env, pi, memop);
    if (atmax == MO_64) {
        if (!HAVE_al8 && (pi & 7) == 0) {
            load_atomic8_or_exit(env, ra, pv);
        }
        return load_atom_extract_al16_or_exit(env, ra, pv, 8);
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
        cpu_loop_exit_atomic(env_cpu(env), ra);
    default:
        g_assert_not_reached();
    }
}
