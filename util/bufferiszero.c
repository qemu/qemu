/*
 * Simple C functions to supplement the C library
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/cutils.h"
#include "qemu/bswap.h"


/* vector definitions */

extern void link_error(void);

#define ACCEL_BUFFER_ZERO(NAME, SIZE, VECTYPE, NONZERO)         \
static bool NAME(const void *buf, size_t len)                   \
{                                                               \
    const void *end = buf + len;                                \
    do {                                                        \
        const VECTYPE *p = buf;                                 \
        VECTYPE t;                                              \
        if (SIZE == sizeof(VECTYPE) * 4) {                      \
            t = (p[0] | p[1]) | (p[2] | p[3]);                  \
        } else if (SIZE == sizeof(VECTYPE) * 8) {               \
            t  = p[0] | p[1];                                   \
            t |= p[2] | p[3];                                   \
            t |= p[4] | p[5];                                   \
            t |= p[6] | p[7];                                   \
        } else {                                                \
            link_error();                                       \
        }                                                       \
        if (unlikely(NONZERO(t))) {                             \
            return false;                                       \
        }                                                       \
        buf += SIZE;                                            \
    } while (buf < end);                                        \
    return true;                                                \
}

static bool
buffer_zero_int(const void *buf, size_t len)
{
    if (unlikely(len < 8)) {
        /* For a very small buffer, simply accumulate all the bytes.  */
        const unsigned char *p = buf;
        const unsigned char *e = buf + len;
        unsigned char t = 0;

        do {
            t |= *p++;
        } while (p < e);

        return t == 0;
    } else {
        /* Otherwise, use the unaligned memory access functions to
           handle the beginning and end of the buffer, with a couple
           of loops handling the middle aligned section.  */
        uint64_t t = ldq_he_p(buf);
        const uint64_t *p = (uint64_t *)(((uintptr_t)buf + 8) & -8);
        const uint64_t *e = (uint64_t *)(((uintptr_t)buf + len) & -8);

        for (; p + 8 <= e; p += 8) {
            __builtin_prefetch(p + 8);
            if (t) {
                return false;
            }
            t = p[0] | p[1] | p[2] | p[3] | p[4] | p[5] | p[6] | p[7];
        }
        while (p < e) {
            t |= *p++;
        }
        t |= ldq_he_p(buf + len - 8);

        return t == 0;
    }
}

#if defined(CONFIG_AVX2_OPT) || (defined(CONFIG_CPUID_H) && defined(__SSE2__))
#include <cpuid.h>

/* Do not use push_options pragmas unnecessarily, because clang
 * does not support them.
 */
#ifndef __SSE2__
#pragma GCC push_options
#pragma GCC target("sse2")
#endif
#include <emmintrin.h>
#define SSE2_NONZERO(X) \
    (_mm_movemask_epi8(_mm_cmpeq_epi8((X), _mm_setzero_si128())) != 0xFFFF)
ACCEL_BUFFER_ZERO(buffer_zero_sse2, 64, __m128i, SSE2_NONZERO)
#ifndef __SSE2__
#pragma GCC pop_options
#endif

#ifdef CONFIG_AVX2_OPT
#pragma GCC push_options
#pragma GCC target("sse4")
#include <smmintrin.h>
#define SSE4_NONZERO(X)  !_mm_testz_si128((X), (X))
ACCEL_BUFFER_ZERO(buffer_zero_sse4, 64, __m128i, SSE4_NONZERO)
#pragma GCC pop_options

#pragma GCC push_options
#pragma GCC target("avx2")
#include <immintrin.h>
#define AVX2_NONZERO(X)  !_mm256_testz_si256((X), (X))
ACCEL_BUFFER_ZERO(buffer_zero_avx2, 128, __m256i, AVX2_NONZERO)
#pragma GCC pop_options
#endif

#define CACHE_AVX2    2
#define CACHE_AVX1    4
#define CACHE_SSE4    8
#define CACHE_SSE2    16

static unsigned cpuid_cache;

static void __attribute__((constructor)) init_cpuid_cache(void)
{
    int max = __get_cpuid_max(0, NULL);
    int a, b, c, d;
    unsigned cache = 0;

    if (max >= 1) {
        __cpuid(1, a, b, c, d);
        if (d & bit_SSE2) {
            cache |= CACHE_SSE2;
        }
#ifdef CONFIG_AVX2_OPT
        if (c & bit_SSE4_1) {
            cache |= CACHE_SSE4;
        }

        /* We must check that AVX is not just available, but usable.  */
        if ((c & bit_OSXSAVE) && (c & bit_AVX)) {
            __asm("xgetbv" : "=a"(a), "=d"(d) : "c"(0));
            if ((a & 6) == 6) {
                cache |= CACHE_AVX1;
                if (max >= 7) {
                    __cpuid_count(7, 0, a, b, c, d);
                    if (b & bit_AVX2) {
                        cache |= CACHE_AVX2;
                    }
                }
            }
        }
#endif
    }
    cpuid_cache = cache;
}

#define HAVE_NEXT_ACCEL
bool test_buffer_is_zero_next_accel(void)
{
    /* If no bits set, we just tested buffer_zero_int, and there
       are no more acceleration options to test.  */
    if (cpuid_cache == 0) {
        return false;
    }
    /* Disable the accelerator we used before and select a new one.  */
    cpuid_cache &= cpuid_cache - 1;
    return true;
}

static bool select_accel_fn(const void *buf, size_t len)
{
    uintptr_t ibuf = (uintptr_t)buf;
#ifdef CONFIG_AVX2_OPT
    if (len % 128 == 0 && ibuf % 32 == 0 && (cpuid_cache & CACHE_AVX2)) {
        return buffer_zero_avx2(buf, len);
    }
    if (len % 64 == 0 && ibuf % 16 == 0 && (cpuid_cache & CACHE_SSE4)) {
        return buffer_zero_sse4(buf, len);
    }
#endif
    if (len % 64 == 0 && ibuf % 16 == 0 && (cpuid_cache & CACHE_SSE2)) {
        return buffer_zero_sse2(buf, len);
    }
    return buffer_zero_int(buf, len);
}

#else
#define select_accel_fn  buffer_zero_int
#endif

#ifndef HAVE_NEXT_ACCEL
bool test_buffer_is_zero_next_accel(void)
{
    return false;
}
#endif

/*
 * Checks if a buffer is all zeroes
 */
bool buffer_is_zero(const void *buf, size_t len)
{
    if (unlikely(len == 0)) {
        return true;
    }

    /* Use an optimized zero check if possible.  Note that this also
       includes a check for an unrolled loop over 64-bit integers.  */
    return select_accel_fn(buf, len);
}
