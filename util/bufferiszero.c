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

static bool (*buffer_is_zero_accel)(const void *, size_t);

static bool buffer_is_zero_integer(const void *buf, size_t len)
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

static unsigned __attribute__((noinline))
select_accel_cpuinfo(unsigned info)
{
    /* Array is sorted in order of algorithm preference. */
    static const struct {
        unsigned bit;
        bool (*fn)(const void *, size_t);
    } all[] = {
#ifdef CONFIG_AVX2_OPT
        { CPUINFO_AVX2,    buffer_zero_avx2 },
#endif
        { CPUINFO_SSE2,    buffer_zero_sse2 },
        { CPUINFO_ALWAYS,  buffer_is_zero_integer },
    };

    for (unsigned i = 0; i < ARRAY_SIZE(all); ++i) {
        if (info & all[i].bit) {
            buffer_is_zero_accel = all[i].fn;
            return all[i].bit;
        }
    }
    return 0;
}

static unsigned used_accel;

static void __attribute__((constructor)) init_accel(void)
{
    used_accel = select_accel_cpuinfo(cpuinfo_init());
}

#define INIT_ACCEL NULL

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
#else
bool test_buffer_is_zero_next_accel(void)
{
    return false;
}

#define INIT_ACCEL buffer_is_zero_integer
#endif

static bool (*buffer_is_zero_accel)(const void *, size_t) = INIT_ACCEL;

bool buffer_is_zero_ool(const void *buf, size_t len)
{
    if (unlikely(len == 0)) {
        return true;
    }
    if (!buffer_is_zero_sample3(buf, len)) {
        return false;
    }
    /* All bytes are covered for any len <= 3.  */
    if (unlikely(len <= 3)) {
        return true;
    }

    if (likely(len >= 256)) {
        return buffer_is_zero_accel(buf, len);
    }
    return buffer_is_zero_integer(buf, len);
}

bool buffer_is_zero_ge256(const void *buf, size_t len)
{
    return buffer_is_zero_accel(buf, len);
}
