/*
 * Macros for swapping a value if the endianness is different
 * between the target and the host.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef TSWAP_H
#define TSWAP_H

#include "qemu/bswap.h"

/**
 * target_words_bigendian:
 * Returns true if the (default) endianness of the target is big endian,
 * false otherwise. Note that in target-specific code, you can use
 * TARGET_BIG_ENDIAN directly instead. On the other hand, common
 * code should normally never need to know about the endianness of the
 * target, so please do *not* use this function unless you know very well
 * what you are doing!
 */
bool target_words_bigendian(void);

/*
 * If we're in target-specific code, we can hard-code the swapping
 * condition, otherwise we have to do (slower) run-time checks.
 */
#ifdef COMPILING_PER_TARGET
#define target_needs_bswap()  (HOST_BIG_ENDIAN != TARGET_BIG_ENDIAN)
#else
#define target_needs_bswap()  (HOST_BIG_ENDIAN != target_words_bigendian())
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
