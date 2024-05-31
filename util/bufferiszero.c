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

typedef bool (*biz_accel_fn)(const void *, size_t);

static bool buffer_is_zero_int_lt256(const void *buf, size_t len)
{
    uint64_t t;
    const uint64_t *p, *e;

    /*
     * Use unaligned memory access functions to handle
     * the beginning and end of the buffer.
     */
    if (unlikely(len <= 8)) {
        return (ldl_he_p(buf) | ldl_he_p(buf + len - 4)) == 0;
    }

    t = ldq_he_p(buf) | ldq_he_p(buf + len - 8);
    p = QEMU_ALIGN_PTR_DOWN(buf + 8, 8);
    e = QEMU_ALIGN_PTR_DOWN(buf + len - 1, 8);

    /* Read 0 to 31 aligned words from the middle. */
    while (p < e) {
        t |= *p++;
    }
    return t == 0;
}

static bool buffer_is_zero_int_ge256(const void *buf, size_t len)
{
    /*
     * Use unaligned memory access functions to handle
     * the beginning and end of the buffer.
     */
    uint64_t t = ldq_he_p(buf) | ldq_he_p(buf + len - 8);
    const uint64_t *p = QEMU_ALIGN_PTR_DOWN(buf + 8, 8);
    const uint64_t *e = QEMU_ALIGN_PTR_DOWN(buf + len - 1, 8);

    /* Collect a partial block at the tail end. */
    t |= e[-7] | e[-6] | e[-5] | e[-4] | e[-3] | e[-2] | e[-1];

    /*
     * Loop over 64 byte blocks.
     * With the head and tail removed, e - p >= 30,
     * so the loop must iterate at least 3 times.
     */
    do {
        if (t) {
            return false;
        }
        t = p[0] | p[1] | p[2] | p[3] | p[4] | p[5] | p[6] | p[7];
        p += 8;
    } while (p < e - 7);

    return t == 0;
}

#if defined(CONFIG_AVX2_OPT) || defined(__SSE2__)
#include <immintrin.h>

/* Helper for preventing the compiler from reassociating
   chains of binary vector operations.  */
#define SSE_REASSOC_BARRIER(vec0, vec1) asm("" : "+x"(vec0), "+x"(vec1))

/* Note that these vectorized functions may assume len >= 256.  */

static bool __attribute__((target("sse2")))
buffer_zero_sse2(const void *buf, size_t len)
{
    /* Unaligned loads at head/tail.  */
    __m128i v = *(__m128i_u *)(buf);
    __m128i w = *(__m128i_u *)(buf + len - 16);
    /* Align head/tail to 16-byte boundaries.  */
    const __m128i *p = QEMU_ALIGN_PTR_DOWN(buf + 16, 16);
    const __m128i *e = QEMU_ALIGN_PTR_DOWN(buf + len - 1, 16);
    __m128i zero = { 0 };

    /* Collect a partial block at tail end.  */
    v |= e[-1]; w |= e[-2];
    SSE_REASSOC_BARRIER(v, w);
    v |= e[-3]; w |= e[-4];
    SSE_REASSOC_BARRIER(v, w);
    v |= e[-5]; w |= e[-6];
    SSE_REASSOC_BARRIER(v, w);
    v |= e[-7]; v |= w;

    /*
     * Loop over complete 128-byte blocks.
     * With the head and tail removed, e - p >= 14, so the loop
     * must iterate at least once.
     */
    do {
        v = _mm_cmpeq_epi8(v, zero);
        if (unlikely(_mm_movemask_epi8(v) != 0xFFFF)) {
            return false;
        }
        v = p[0]; w = p[1];
        SSE_REASSOC_BARRIER(v, w);
        v |= p[2]; w |= p[3];
        SSE_REASSOC_BARRIER(v, w);
        v |= p[4]; w |= p[5];
        SSE_REASSOC_BARRIER(v, w);
        v |= p[6]; w |= p[7];
        SSE_REASSOC_BARRIER(v, w);
        v |= w;
        p += 8;
    } while (p < e - 7);

    return _mm_movemask_epi8(_mm_cmpeq_epi8(v, zero)) == 0xFFFF;
}

#ifdef CONFIG_AVX2_OPT
static bool __attribute__((target("avx2")))
buffer_zero_avx2(const void *buf, size_t len)
{
    /* Unaligned loads at head/tail.  */
    __m256i v = *(__m256i_u *)(buf);
    __m256i w = *(__m256i_u *)(buf + len - 32);
    /* Align head/tail to 32-byte boundaries.  */
    const __m256i *p = QEMU_ALIGN_PTR_DOWN(buf + 32, 32);
    const __m256i *e = QEMU_ALIGN_PTR_DOWN(buf + len - 1, 32);
    __m256i zero = { 0 };

    /* Collect a partial block at tail end.  */
    v |= e[-1]; w |= e[-2];
    SSE_REASSOC_BARRIER(v, w);
    v |= e[-3]; w |= e[-4];
    SSE_REASSOC_BARRIER(v, w);
    v |= e[-5]; w |= e[-6];
    SSE_REASSOC_BARRIER(v, w);
    v |= e[-7]; v |= w;

    /* Loop over complete 256-byte blocks.  */
    for (; p < e - 7; p += 8) {
        /* PTEST is not profitable here.  */
        v = _mm256_cmpeq_epi8(v, zero);
        if (unlikely(_mm256_movemask_epi8(v) != 0xFFFFFFFF)) {
            return false;
        }
        v = p[0]; w = p[1];
        SSE_REASSOC_BARRIER(v, w);
        v |= p[2]; w |= p[3];
        SSE_REASSOC_BARRIER(v, w);
        v |= p[4]; w |= p[5];
        SSE_REASSOC_BARRIER(v, w);
        v |= p[6]; w |= p[7];
        SSE_REASSOC_BARRIER(v, w);
        v |= w;
    }

    return _mm256_movemask_epi8(_mm256_cmpeq_epi8(v, zero)) == 0xFFFFFFFF;
}
#endif /* CONFIG_AVX2_OPT */

static biz_accel_fn const accel_table[] = {
    buffer_is_zero_int_ge256,
    buffer_zero_sse2,
#ifdef CONFIG_AVX2_OPT
    buffer_zero_avx2,
#endif
};

static unsigned best_accel(void)
{
#ifdef CONFIG_AVX2_OPT
    unsigned info = cpuinfo_init();

    if (info & CPUINFO_AVX2) {
        return 2;
    }
#endif
    return 1;
}

#elif defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>

/*
 * Helper for preventing the compiler from reassociating
 * chains of binary vector operations.
 */
#define REASSOC_BARRIER(vec0, vec1) asm("" : "+w"(vec0), "+w"(vec1))

static bool buffer_is_zero_simd(const void *buf, size_t len)
{
    uint32x4_t t0, t1, t2, t3;

    /* Align head/tail to 16-byte boundaries.  */
    const uint32x4_t *p = QEMU_ALIGN_PTR_DOWN(buf + 16, 16);
    const uint32x4_t *e = QEMU_ALIGN_PTR_DOWN(buf + len - 1, 16);

    /* Unaligned loads at head/tail.  */
    t0 = vld1q_u32(buf) | vld1q_u32(buf + len - 16);

    /* Collect a partial block at tail end.  */
    t1 = e[-7] | e[-6];
    t2 = e[-5] | e[-4];
    t3 = e[-3] | e[-2];
    t0 |= e[-1];
    REASSOC_BARRIER(t0, t1);
    REASSOC_BARRIER(t2, t3);
    t0 |= t1;
    t2 |= t3;
    REASSOC_BARRIER(t0, t2);
    t0 |= t2;

    /*
     * Loop over complete 128-byte blocks.
     * With the head and tail removed, e - p >= 14, so the loop
     * must iterate at least once.
     */
    do {
        /*
         * Reduce via UMAXV.  Whatever the actual result,
         * it will only be zero if all input bytes are zero.
         */
        if (unlikely(vmaxvq_u32(t0) != 0)) {
            return false;
        }

        t0 = p[0] | p[1];
        t1 = p[2] | p[3];
        t2 = p[4] | p[5];
        t3 = p[6] | p[7];
        REASSOC_BARRIER(t0, t1);
        REASSOC_BARRIER(t2, t3);
        t0 |= t1;
        t2 |= t3;
        REASSOC_BARRIER(t0, t2);
        t0 |= t2;
        p += 8;
    } while (p < e - 7);

    return vmaxvq_u32(t0) == 0;
}

#define best_accel() 1
static biz_accel_fn const accel_table[] = {
    buffer_is_zero_int_ge256,
    buffer_is_zero_simd,
};
#else
#define best_accel() 0
static biz_accel_fn const accel_table[1] = {
    buffer_is_zero_int_ge256
};
#endif

static biz_accel_fn buffer_is_zero_accel;
static unsigned accel_index;

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
    return buffer_is_zero_int_lt256(buf, len);
}

bool buffer_is_zero_ge256(const void *buf, size_t len)
{
    return buffer_is_zero_accel(buf, len);
}

bool test_buffer_is_zero_next_accel(void)
{
    if (accel_index != 0) {
        buffer_is_zero_accel = accel_table[--accel_index];
        return true;
    }
    return false;
}

static void __attribute__((constructor)) init_accel(void)
{
    accel_index = best_accel();
    buffer_is_zero_accel = accel_table[accel_index];
}
