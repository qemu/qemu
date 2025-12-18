/*
 * x86 specific aes acceleration.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef X86_HOST_CRYPTO_AES_ROUND_H
#define X86_HOST_CRYPTO_AES_ROUND_H

#include "host/cpuinfo.h"
#include <immintrin.h>

#if defined(__AES__) && defined(__SSSE3__)
# define HAVE_AES_ACCEL  true
# define ATTR_AES_ACCEL
#else
# define HAVE_AES_ACCEL  likely(cpuinfo & CPUINFO_AES)
# define ATTR_AES_ACCEL  __attribute__((target("aes,ssse3")))
#endif

static inline __m128i ATTR_AES_ACCEL
aes_accel_bswap(__m128i x)
{
    return _mm_shuffle_epi8(x, _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8,
                                            9, 10, 11, 12, 13, 14, 15));
}

static inline void ATTR_AES_ACCEL
aesenc_MC_accel(AESState *ret, const AESState *st, bool be)
{
    __m128i t = (__m128i)st->v;
    __m128i z = _mm_setzero_si128();

    if (be) {
        t = aes_accel_bswap(t);
        t = _mm_aesdeclast_si128(t, z);
        t = _mm_aesenc_si128(t, z);
        t = aes_accel_bswap(t);
    } else {
        t = _mm_aesdeclast_si128(t, z);
        t = _mm_aesenc_si128(t, z);
    }
    ret->v = (AESStateVec)t;
}

static inline void ATTR_AES_ACCEL
aesenc_SB_SR_AK_accel(AESState *ret, const AESState *st,
                      const AESState *rk, bool be)
{
    __m128i t = (__m128i)st->v;
    __m128i k = (__m128i)rk->v;

    if (be) {
        t = aes_accel_bswap(t);
        k = aes_accel_bswap(k);
        t = _mm_aesenclast_si128(t, k);
        t = aes_accel_bswap(t);
    } else {
        t = _mm_aesenclast_si128(t, k);
    }
    ret->v = (AESStateVec)t;
}

static inline void ATTR_AES_ACCEL
aesenc_SB_SR_MC_AK_accel(AESState *ret, const AESState *st,
                         const AESState *rk, bool be)
{
    __m128i t = (__m128i)st->v;
    __m128i k = (__m128i)rk->v;

    if (be) {
        t = aes_accel_bswap(t);
        k = aes_accel_bswap(k);
        t = _mm_aesenc_si128(t, k);
        t = aes_accel_bswap(t);
    } else {
        t = _mm_aesenc_si128(t, k);
    }
    ret->v = (AESStateVec)t;
}

static inline void ATTR_AES_ACCEL
aesdec_IMC_accel(AESState *ret, const AESState *st, bool be)
{
    __m128i t = (__m128i)st->v;

    if (be) {
        t = aes_accel_bswap(t);
        t = _mm_aesimc_si128(t);
        t = aes_accel_bswap(t);
    } else {
        t = _mm_aesimc_si128(t);
    }
    ret->v = (AESStateVec)t;
}

static inline void ATTR_AES_ACCEL
aesdec_ISB_ISR_AK_accel(AESState *ret, const AESState *st,
                        const AESState *rk, bool be)
{
    __m128i t = (__m128i)st->v;
    __m128i k = (__m128i)rk->v;

    if (be) {
        t = aes_accel_bswap(t);
        k = aes_accel_bswap(k);
        t = _mm_aesdeclast_si128(t, k);
        t = aes_accel_bswap(t);
    } else {
        t = _mm_aesdeclast_si128(t, k);
    }
    ret->v = (AESStateVec)t;
}

static inline void ATTR_AES_ACCEL
aesdec_ISB_ISR_AK_IMC_accel(AESState *ret, const AESState *st,
                            const AESState *rk, bool be)
{
    __m128i t = (__m128i)st->v;
    __m128i k = (__m128i)rk->v;

    if (be) {
        t = aes_accel_bswap(t);
        k = aes_accel_bswap(k);
        t = _mm_aesdeclast_si128(t, k);
        t = _mm_aesimc_si128(t);
        t = aes_accel_bswap(t);
    } else {
        t = _mm_aesdeclast_si128(t, k);
        t = _mm_aesimc_si128(t);
    }
    ret->v = (AESStateVec)t;
}

static inline void ATTR_AES_ACCEL
aesdec_ISB_ISR_IMC_AK_accel(AESState *ret, const AESState *st,
                            const AESState *rk, bool be)
{
    __m128i t = (__m128i)st->v;
    __m128i k = (__m128i)rk->v;

    if (be) {
        t = aes_accel_bswap(t);
        k = aes_accel_bswap(k);
        t = _mm_aesdec_si128(t, k);
        t = aes_accel_bswap(t);
    } else {
        t = _mm_aesdec_si128(t, k);
    }
    ret->v = (AESStateVec)t;
}

#endif /* X86_HOST_CRYPTO_AES_ROUND_H */
