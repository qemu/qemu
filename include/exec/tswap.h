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
 * false otherwise. Common code should normally never need to know about the
 * endianness of the target, so please do *not* use this function unless you
 * know very well what you are doing!
 */
bool target_words_bigendian(void);
#ifdef COMPILING_PER_TARGET
#define target_words_bigendian()  TARGET_BIG_ENDIAN
#endif

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

/* Return ld{word}_{le,be}_p following target endianness. */
#define LOAD_IMPL(word, args...)                    \
do {                                                \
    if (target_words_bigendian()) {                 \
        return glue(glue(ld, word), _be_p)(args);   \
    } else {                                        \
        return glue(glue(ld, word), _le_p)(args);   \
    }                                               \
} while (0)

static inline int lduw_p(const void *ptr)
{
    LOAD_IMPL(uw, ptr);
}

static inline int ldsw_p(const void *ptr)
{
    LOAD_IMPL(sw, ptr);
}

static inline int ldl_p(const void *ptr)
{
    LOAD_IMPL(l, ptr);
}

static inline uint64_t ldq_p(const void *ptr)
{
    LOAD_IMPL(q, ptr);
}

static inline uint64_t ldn_p(const void *ptr, int sz)
{
    LOAD_IMPL(n, ptr, sz);
}

#undef LOAD_IMPL

/* Call st{word}_{le,be}_p following target endianness. */
#define STORE_IMPL(word, args...)           \
do {                                        \
    if (target_words_bigendian()) {         \
        glue(glue(st, word), _be_p)(args);  \
    } else {                                \
        glue(glue(st, word), _le_p)(args);  \
    }                                       \
} while (0)


static inline void stw_p(void *ptr, uint16_t v)
{
    STORE_IMPL(w, ptr, v);
}

static inline void stl_p(void *ptr, uint32_t v)
{
    STORE_IMPL(l, ptr, v);
}

static inline void stq_p(void *ptr, uint64_t v)
{
    STORE_IMPL(q, ptr, v);
}

static inline void stn_p(void *ptr, int sz, uint64_t v)
{
    STORE_IMPL(n, ptr, sz, v);
}

#undef STORE_IMPL

#endif  /* TSWAP_H */
