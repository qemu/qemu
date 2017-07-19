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

#if defined(CONFIG_AVX2_OPT) || defined(__SSE2__)
/* Do not use push_options pragmas unnecessarily, because clang
 * does not support them.
 */
#ifdef CONFIG_AVX2_OPT
#pragma GCC push_options
#pragma GCC target("sse2")
#endif
#include <emmintrin.h>

/* Note that each of these vectorized functions require len >= 64.  */

static bool
buffer_zero_sse2(const void *buf, size_t len)
{
    __m128i t = _mm_loadu_si128(buf);
    __m128i *p = (__m128i *)(((uintptr_t)buf + 5 * 16) & -16);
    __m128i *e = (__m128i *)(((uintptr_t)buf + len) & -16);
    __m128i zero = _mm_setzero_si128();

    /* Loop over 16-byte aligned blocks of 64.  */
    while (likely(p <= e)) {
        __builtin_prefetch(p);
        t = _mm_cmpeq_epi8(t, zero);
        if (unlikely(_mm_movemask_epi8(t) != 0xFFFF)) {
            return false;
        }
        t = p[-4] | p[-3] | p[-2] | p[-1];
        p += 4;
    }

    /* Finish the aligned tail.  */
    t |= e[-3];
    t |= e[-2];
    t |= e[-1];

    /* Finish the unaligned tail.  */
    t |= _mm_loadu_si128(buf + len - 16);

    return _mm_movemask_epi8(_mm_cmpeq_epi8(t, zero)) == 0xFFFF;
}
#ifdef CONFIG_AVX2_OPT
#pragma GCC pop_options
#endif

#ifdef CONFIG_AVX2_OPT
/* Note that due to restrictions/bugs wrt __builtin functions in gcc <= 4.8,
 * the includes have to be within the corresponding push_options region, and
 * therefore the regions themselves have to be ordered with increasing ISA.
 */
#pragma GCC push_options
#pragma GCC target("sse4")
#include <smmintrin.h>

static bool
buffer_zero_sse4(const void *buf, size_t len)
{
    __m128i t = _mm_loadu_si128(buf);
    __m128i *p = (__m128i *)(((uintptr_t)buf + 5 * 16) & -16);
    __m128i *e = (__m128i *)(((uintptr_t)buf + len) & -16);

    /* Loop over 16-byte aligned blocks of 64.  */
    while (likely(p <= e)) {
        __builtin_prefetch(p);
        if (unlikely(!_mm_testz_si128(t, t))) {
            return false;
        }
        t = p[-4] | p[-3] | p[-2] | p[-1];
        p += 4;
    }

    /* Finish the aligned tail.  */
    t |= e[-3];
    t |= e[-2];
    t |= e[-1];

    /* Finish the unaligned tail.  */
    t |= _mm_loadu_si128(buf + len - 16);

    return _mm_testz_si128(t, t);
}

#pragma GCC pop_options
#pragma GCC push_options
#pragma GCC target("avx2")
#include <immintrin.h>

static bool
buffer_zero_avx2(const void *buf, size_t len)
{
    /* Begin with an unaligned head of 32 bytes.  */
    __m256i t = _mm256_loadu_si256(buf);
    __m256i *p = (__m256i *)(((uintptr_t)buf + 5 * 32) & -32);
    __m256i *e = (__m256i *)(((uintptr_t)buf + len) & -32);

    if (likely(p <= e)) {
        /* Loop over 32-byte aligned blocks of 128.  */
        do {
            __builtin_prefetch(p);
            if (unlikely(!_mm256_testz_si256(t, t))) {
                return false;
            }
            t = p[-4] | p[-3] | p[-2] | p[-1];
            p += 4;
        } while (p <= e);
    } else {
        t |= _mm256_loadu_si256(buf + 32);
        if (len <= 128) {
            goto last2;
        }
    }

    /* Finish the last block of 128 unaligned.  */
    t |= _mm256_loadu_si256(buf + len - 4 * 32);
    t |= _mm256_loadu_si256(buf + len - 3 * 32);
 last2:
    t |= _mm256_loadu_si256(buf + len - 2 * 32);
    t |= _mm256_loadu_si256(buf + len - 1 * 32);

    return _mm256_testz_si256(t, t);
}
#pragma GCC pop_options
#endif /* CONFIG_AVX2_OPT */

/* Note that for test_buffer_is_zero_next_accel, the most preferred
 * ISA must have the least significant bit.
 */
#define CACHE_AVX2    1
#define CACHE_SSE4    2
#define CACHE_SSE2    4

/* Make sure that these variables are appropriately initialized when
 * SSE2 is enabled on the compiler command-line, but the compiler is
 * too old to support CONFIG_AVX2_OPT.
 */
#ifdef CONFIG_AVX2_OPT
# define INIT_CACHE 0
# define INIT_ACCEL buffer_zero_int
#else
# ifndef __SSE2__
#  error "ISA selection confusion"
# endif
# define INIT_CACHE CACHE_SSE2
# define INIT_ACCEL buffer_zero_sse2
#endif

static unsigned cpuid_cache = INIT_CACHE;
static bool (*buffer_accel)(const void *, size_t) = INIT_ACCEL;

static void init_accel(unsigned cache)
{
    bool (*fn)(const void *, size_t) = buffer_zero_int;
    if (cache & CACHE_SSE2) {
        fn = buffer_zero_sse2;
    }
#ifdef CONFIG_AVX2_OPT
    if (cache & CACHE_SSE4) {
        fn = buffer_zero_sse4;
    }
    if (cache & CACHE_AVX2) {
        fn = buffer_zero_avx2;
    }
#endif
    buffer_accel = fn;
}

#ifdef CONFIG_AVX2_OPT
#include "qemu/cpuid.h"

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
        if (c & bit_SSE4_1) {
            cache |= CACHE_SSE4;
        }

        /* We must check that AVX is not just available, but usable.  */
        if ((c & bit_OSXSAVE) && (c & bit_AVX) && max >= 7) {
            int bv;
            __asm("xgetbv" : "=a"(bv), "=d"(d) : "c"(0));
            __cpuid_count(7, 0, a, b, c, d);
            if ((bv & 6) == 6 && (b & bit_AVX2)) {
                cache |= CACHE_AVX2;
            }
        }
    }
    cpuid_cache = cache;
    init_accel(cache);
}
#endif /* CONFIG_AVX2_OPT */

bool test_buffer_is_zero_next_accel(void)
{
    /* If no bits set, we just tested buffer_zero_int, and there
       are no more acceleration options to test.  */
    if (cpuid_cache == 0) {
        return false;
    }
    /* Disable the accelerator we used before and select a new one.  */
    cpuid_cache &= cpuid_cache - 1;
    init_accel(cpuid_cache);
    return true;
}

static bool select_accel_fn(const void *buf, size_t len)
{
    if (likely(len >= 64)) {
        return buffer_accel(buf, len);
    }
    return buffer_zero_int(buf, len);
}

#else
#define select_accel_fn  buffer_zero_int
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

    /* Fetch the beginning of the buffer while we select the accelerator.  */
    __builtin_prefetch(buf);

    /* Use an optimized zero check if possible.  Note that this also
       includes a check for an unrolled loop over 64-bit integers.  */
    return select_accel_fn(buf, len);
}
