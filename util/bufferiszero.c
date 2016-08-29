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


/* vector definitions */
#ifdef __ALTIVEC__
#include <altivec.h>
/* The altivec.h header says we're allowed to undef these for
 * C++ compatibility.  Here we don't care about C++, but we
 * undef them anyway to avoid namespace pollution.
 */
#undef vector
#undef pixel
#undef bool
#define VECTYPE        __vector unsigned char
#define SPLAT(p)       vec_splat(vec_ld(0, p), 0)
#define ALL_EQ(v1, v2) vec_all_eq(v1, v2)
#define VEC_OR(v1, v2) ((v1) | (v2))
/* altivec.h may redefine the bool macro as vector type.
 * Reset it to POSIX semantics. */
#define bool _Bool
#elif defined __SSE2__
#include <emmintrin.h>
#define VECTYPE        __m128i
#define SPLAT(p)       _mm_set1_epi8(*(p))
#define ALL_EQ(v1, v2) (_mm_movemask_epi8(_mm_cmpeq_epi8(v1, v2)) == 0xFFFF)
#define VEC_OR(v1, v2) (_mm_or_si128(v1, v2))
#elif defined(__aarch64__)
#include "arm_neon.h"
#define VECTYPE        uint64x2_t
#define ALL_EQ(v1, v2) \
        ((vgetq_lane_u64(v1, 0) == vgetq_lane_u64(v2, 0)) && \
         (vgetq_lane_u64(v1, 1) == vgetq_lane_u64(v2, 1)))
#define VEC_OR(v1, v2) ((v1) | (v2))
#else
#define VECTYPE        unsigned long
#define SPLAT(p)       (*(p) * (~0UL / 255))
#define ALL_EQ(v1, v2) ((v1) == (v2))
#define VEC_OR(v1, v2) ((v1) | (v2))
#endif

#define BUFFER_FIND_NONZERO_OFFSET_UNROLL_FACTOR 8

static bool
can_use_buffer_find_nonzero_offset_inner(const void *buf, size_t len)
{
    return (len % (BUFFER_FIND_NONZERO_OFFSET_UNROLL_FACTOR
                   * sizeof(VECTYPE)) == 0
            && ((uintptr_t) buf) % sizeof(VECTYPE) == 0);
}

/*
 * Searches for an area with non-zero content in a buffer
 *
 * Attention! The len must be a multiple of
 * BUFFER_FIND_NONZERO_OFFSET_UNROLL_FACTOR * sizeof(VECTYPE)
 * and addr must be a multiple of sizeof(VECTYPE) due to
 * restriction of optimizations in this function.
 *
 * can_use_buffer_find_nonzero_offset_inner() can be used to
 * check these requirements.
 *
 * The return value is the offset of the non-zero area rounded
 * down to a multiple of sizeof(VECTYPE) for the first
 * BUFFER_FIND_NONZERO_OFFSET_UNROLL_FACTOR chunks and down to
 * BUFFER_FIND_NONZERO_OFFSET_UNROLL_FACTOR * sizeof(VECTYPE)
 * afterwards.
 *
 * If the buffer is all zero the return value is equal to len.
 */

static size_t buffer_find_nonzero_offset_inner(const void *buf, size_t len)
{
    const VECTYPE *p = buf;
    const VECTYPE zero = (VECTYPE){0};
    size_t i;

    assert(can_use_buffer_find_nonzero_offset_inner(buf, len));

    if (!len) {
        return 0;
    }

    for (i = 0; i < BUFFER_FIND_NONZERO_OFFSET_UNROLL_FACTOR; i++) {
        if (!ALL_EQ(p[i], zero)) {
            return i * sizeof(VECTYPE);
        }
    }

    for (i = BUFFER_FIND_NONZERO_OFFSET_UNROLL_FACTOR;
         i < len / sizeof(VECTYPE);
         i += BUFFER_FIND_NONZERO_OFFSET_UNROLL_FACTOR) {
        VECTYPE tmp0 = VEC_OR(p[i + 0], p[i + 1]);
        VECTYPE tmp1 = VEC_OR(p[i + 2], p[i + 3]);
        VECTYPE tmp2 = VEC_OR(p[i + 4], p[i + 5]);
        VECTYPE tmp3 = VEC_OR(p[i + 6], p[i + 7]);
        VECTYPE tmp01 = VEC_OR(tmp0, tmp1);
        VECTYPE tmp23 = VEC_OR(tmp2, tmp3);
        if (!ALL_EQ(VEC_OR(tmp01, tmp23), zero)) {
            break;
        }
    }

    return i * sizeof(VECTYPE);
}

#if defined CONFIG_AVX2_OPT
#pragma GCC push_options
#pragma GCC target("avx2")
#include <cpuid.h>
#include <immintrin.h>

#define AVX2_VECTYPE        __m256i
#define AVX2_SPLAT(p)       _mm256_set1_epi8(*(p))
#define AVX2_ALL_EQ(v1, v2) \
    (_mm256_movemask_epi8(_mm256_cmpeq_epi8(v1, v2)) == 0xFFFFFFFF)
#define AVX2_VEC_OR(v1, v2) (_mm256_or_si256(v1, v2))

static bool
can_use_buffer_find_nonzero_offset_avx2(const void *buf, size_t len)
{
    return (len % (BUFFER_FIND_NONZERO_OFFSET_UNROLL_FACTOR
                   * sizeof(AVX2_VECTYPE)) == 0
            && ((uintptr_t) buf) % sizeof(AVX2_VECTYPE) == 0);
}

static size_t buffer_find_nonzero_offset_avx2(const void *buf, size_t len)
{
    const AVX2_VECTYPE *p = buf;
    const AVX2_VECTYPE zero = (AVX2_VECTYPE){0};
    size_t i;

    assert(can_use_buffer_find_nonzero_offset_avx2(buf, len));

    if (!len) {
        return 0;
    }

    for (i = 0; i < BUFFER_FIND_NONZERO_OFFSET_UNROLL_FACTOR; i++) {
        if (!AVX2_ALL_EQ(p[i], zero)) {
            return i * sizeof(AVX2_VECTYPE);
        }
    }

    for (i = BUFFER_FIND_NONZERO_OFFSET_UNROLL_FACTOR;
         i < len / sizeof(AVX2_VECTYPE);
         i += BUFFER_FIND_NONZERO_OFFSET_UNROLL_FACTOR) {
        AVX2_VECTYPE tmp0 = AVX2_VEC_OR(p[i + 0], p[i + 1]);
        AVX2_VECTYPE tmp1 = AVX2_VEC_OR(p[i + 2], p[i + 3]);
        AVX2_VECTYPE tmp2 = AVX2_VEC_OR(p[i + 4], p[i + 5]);
        AVX2_VECTYPE tmp3 = AVX2_VEC_OR(p[i + 6], p[i + 7]);
        AVX2_VECTYPE tmp01 = AVX2_VEC_OR(tmp0, tmp1);
        AVX2_VECTYPE tmp23 = AVX2_VEC_OR(tmp2, tmp3);
        if (!AVX2_ALL_EQ(AVX2_VEC_OR(tmp01, tmp23), zero)) {
            break;
        }
    }

    return i * sizeof(AVX2_VECTYPE);
}

static bool avx2_support(void)
{
    int a, b, c, d;

    if (__get_cpuid_max(0, NULL) < 7) {
        return false;
    }

    __cpuid_count(7, 0, a, b, c, d);

    return b & bit_AVX2;
}

bool can_use_buffer_find_nonzero_offset(const void *buf, size_t len) \
         __attribute__ ((ifunc("can_use_buffer_find_nonzero_offset_ifunc")));
size_t buffer_find_nonzero_offset(const void *buf, size_t len) \
         __attribute__ ((ifunc("buffer_find_nonzero_offset_ifunc")));

static void *buffer_find_nonzero_offset_ifunc(void)
{
    typeof(buffer_find_nonzero_offset) *func = (avx2_support()) ?
        buffer_find_nonzero_offset_avx2 : buffer_find_nonzero_offset_inner;

    return func;
}

static void *can_use_buffer_find_nonzero_offset_ifunc(void)
{
    typeof(can_use_buffer_find_nonzero_offset) *func = (avx2_support()) ?
        can_use_buffer_find_nonzero_offset_avx2 :
        can_use_buffer_find_nonzero_offset_inner;

    return func;
}
#pragma GCC pop_options
#else
bool can_use_buffer_find_nonzero_offset(const void *buf, size_t len)
{
    return can_use_buffer_find_nonzero_offset_inner(buf, len);
}

size_t buffer_find_nonzero_offset(const void *buf, size_t len)
{
    return buffer_find_nonzero_offset_inner(buf, len);
}
#endif

/*
 * Checks if a buffer is all zeroes
 *
 * Attention! The len must be a multiple of 4 * sizeof(long) due to
 * restriction of optimizations in this function.
 */
bool buffer_is_zero(const void *buf, size_t len)
{
    /*
     * Use long as the biggest available internal data type that fits into the
     * CPU register and unroll the loop to smooth out the effect of memory
     * latency.
     */

    size_t i;
    long d0, d1, d2, d3;
    const long * const data = buf;

    /* use vector optimized zero check if possible */
    if (can_use_buffer_find_nonzero_offset(buf, len)) {
        return buffer_find_nonzero_offset(buf, len) == len;
    }

    assert(len % (4 * sizeof(long)) == 0);
    len /= sizeof(long);

    for (i = 0; i < len; i += 4) {
        d0 = data[i + 0];
        d1 = data[i + 1];
        d2 = data[i + 2];
        d3 = data[i + 3];

        if (d0 || d1 || d2 || d3) {
            return false;
        }
    }

    return true;
}

