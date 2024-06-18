/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * buffer_is_zero acceleration, x86 version.
 */

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
    unsigned info = cpuinfo_init();

#ifdef CONFIG_AVX2_OPT
    if (info & CPUINFO_AVX2) {
        return 2;
    }
#endif
    return info & CPUINFO_SSE2 ? 1 : 0;
}

#else
# include "host/include/generic/host/bufferiszero.c.inc"
#endif
