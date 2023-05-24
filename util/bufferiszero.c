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
#include "qemu/cutils.h"
#include "qemu/bswap.h"
#include "host/cpuinfo.h"

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

#if defined(CONFIG_AVX512F_OPT) || defined(CONFIG_AVX2_OPT) || defined(__SSE2__)
#include <immintrin.h>

/* Note that each of these vectorized functions require len >= 64.  */

static bool __attribute__((target("sse2")))
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
static bool __attribute__((target("sse4")))
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

static bool __attribute__((target("avx2")))
buffer_zero_avx2(const void *buf, size_t len)
{
    /* Begin with an unaligned head of 32 bytes.  */
    __m256i t = _mm256_loadu_si256(buf);
    __m256i *p = (__m256i *)(((uintptr_t)buf + 5 * 32) & -32);
    __m256i *e = (__m256i *)(((uintptr_t)buf + len) & -32);

    /* Loop over 32-byte aligned blocks of 128.  */
    while (p <= e) {
        __builtin_prefetch(p);
        if (unlikely(!_mm256_testz_si256(t, t))) {
            return false;
        }
        t = p[-4] | p[-3] | p[-2] | p[-1];
        p += 4;
    } ;

    /* Finish the last block of 128 unaligned.  */
    t |= _mm256_loadu_si256(buf + len - 4 * 32);
    t |= _mm256_loadu_si256(buf + len - 3 * 32);
    t |= _mm256_loadu_si256(buf + len - 2 * 32);
    t |= _mm256_loadu_si256(buf + len - 1 * 32);

    return _mm256_testz_si256(t, t);
}
#endif /* CONFIG_AVX2_OPT */

#ifdef CONFIG_AVX512F_OPT
static bool __attribute__((target("avx512f")))
buffer_zero_avx512(const void *buf, size_t len)
{
    /* Begin with an unaligned head of 64 bytes.  */
    __m512i t = _mm512_loadu_si512(buf);
    __m512i *p = (__m512i *)(((uintptr_t)buf + 5 * 64) & -64);
    __m512i *e = (__m512i *)(((uintptr_t)buf + len) & -64);

    /* Loop over 64-byte aligned blocks of 256.  */
    while (p <= e) {
        __builtin_prefetch(p);
        if (unlikely(_mm512_test_epi64_mask(t, t))) {
            return false;
        }
        t = p[-4] | p[-3] | p[-2] | p[-1];
        p += 4;
    }

    t |= _mm512_loadu_si512(buf + len - 4 * 64);
    t |= _mm512_loadu_si512(buf + len - 3 * 64);
    t |= _mm512_loadu_si512(buf + len - 2 * 64);
    t |= _mm512_loadu_si512(buf + len - 1 * 64);

    return !_mm512_test_epi64_mask(t, t);

}
#endif /* CONFIG_AVX512F_OPT */

/*
 * Make sure that these variables are appropriately initialized when
 * SSE2 is enabled on the compiler command-line, but the compiler is
 * too old to support CONFIG_AVX2_OPT.
 */
#if defined(CONFIG_AVX512F_OPT) || defined(CONFIG_AVX2_OPT)
# define INIT_USED     0
# define INIT_LENGTH   0
# define INIT_ACCEL    buffer_zero_int
#else
# ifndef __SSE2__
#  error "ISA selection confusion"
# endif
# define INIT_USED     CPUINFO_SSE2
# define INIT_LENGTH   64
# define INIT_ACCEL    buffer_zero_sse2
#endif

static unsigned used_accel = INIT_USED;
static unsigned length_to_accel = INIT_LENGTH;
static bool (*buffer_accel)(const void *, size_t) = INIT_ACCEL;

static unsigned __attribute__((noinline))
select_accel_cpuinfo(unsigned info)
{
    /* Array is sorted in order of algorithm preference. */
    static const struct {
        unsigned bit;
        unsigned len;
        bool (*fn)(const void *, size_t);
    } all[] = {
#ifdef CONFIG_AVX512F_OPT
        { CPUINFO_AVX512F, 256, buffer_zero_avx512 },
#endif
#ifdef CONFIG_AVX2_OPT
        { CPUINFO_AVX2,    128, buffer_zero_avx2 },
        { CPUINFO_SSE4,     64, buffer_zero_sse4 },
#endif
        { CPUINFO_SSE2,     64, buffer_zero_sse2 },
        { CPUINFO_ALWAYS,    0, buffer_zero_int },
    };

    for (unsigned i = 0; i < ARRAY_SIZE(all); ++i) {
        if (info & all[i].bit) {
            length_to_accel = all[i].len;
            buffer_accel = all[i].fn;
            return all[i].bit;
        }
    }
    return 0;
}

#if defined(CONFIG_AVX512F_OPT) || defined(CONFIG_AVX2_OPT)
static void __attribute__((constructor)) init_accel(void)
{
    used_accel = select_accel_cpuinfo(cpuinfo_init());
}
#endif /* CONFIG_AVX2_OPT */

bool test_buffer_is_zero_next_accel(void)
{
    /*
     * Accumulate the accelerators that we've already tested, and
     * remove them from the set to test this round.  We'll get back
     * a zero from select_accel_cpuinfo when there are no more.
     */
    unsigned used = select_accel_cpuinfo(cpuinfo & ~used_accel);
    used_accel |= used;
    return used;
}

static bool select_accel_fn(const void *buf, size_t len)
{
    if (likely(len >= length_to_accel)) {
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
