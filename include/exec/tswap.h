/*
 * Macros for swapping a value if the endianness is different
 * between the target and the host.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef TSWAP_H
#define TSWAP_H

#include "qemu/bswap.h"
#include "qemu/target-info.h"

/*
 * If we're in target-specific code, we can hard-code the swapping
 * condition, otherwise we have to do (slower) run-time checks.
 */
#ifdef COMPILING_PER_TARGET
#define target_needs_bswap()  (HOST_BIG_ENDIAN != TARGET_BIG_ENDIAN)
#else
#define target_needs_bswap()  (HOST_BIG_ENDIAN != target_big_endian())
#endif /* COMPILING_PER_TARGET */

static inline uint16_t tswap16(uint16_t s)
{
    if (target_needs_bswap()) {
        return bswap16(s);
    } else {
        return s;
    }
}

static inline uint32_t tswap32(uint32_t s)
{
    if (target_needs_bswap()) {
        return bswap32(s);
    } else {
        return s;
    }
}

static inline uint64_t tswap64(uint64_t s)
{
    if (target_needs_bswap()) {
        return bswap64(s);
    } else {
        return s;
    }
}

static inline void tswap16s(uint16_t *s)
{
    if (target_needs_bswap()) {
        *s = bswap16(*s);
    }
}

static inline void tswap32s(uint32_t *s)
{
    if (target_needs_bswap()) {
        *s = bswap32(*s);
    }
}

static inline void tswap64s(uint64_t *s)
{
    if (target_needs_bswap()) {
        *s = bswap64(*s);
    }
}

#endif  /* TSWAP_H */
