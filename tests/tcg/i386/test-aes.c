/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "../multiarch/test-aes-main.c.inc"
#include <immintrin.h>

static bool test_SB_SR(uint8_t *o, const uint8_t *i)
{
    __m128i vi = _mm_loadu_si128((const __m128i_u *)i);

    /* aesenclast also adds round key, so supply zero. */
    vi = _mm_aesenclast_si128(vi, _mm_setzero_si128());

    _mm_storeu_si128((__m128i_u *)o, vi);
    return true;
}

static bool test_MC(uint8_t *o, const uint8_t *i)
{
    return false;
}

static bool test_SB_SR_MC_AK(uint8_t *o, const uint8_t *i, const uint8_t *k)
{
    __m128i vi = _mm_loadu_si128((const __m128i_u *)i);
    __m128i vk = _mm_loadu_si128((const __m128i_u *)k);

    vi = _mm_aesenc_si128(vi, vk);

    _mm_storeu_si128((__m128i_u *)o, vi);
    return true;
}

static bool test_ISB_ISR(uint8_t *o, const uint8_t *i)
{
    __m128i vi = _mm_loadu_si128((const __m128i_u *)i);

    /* aesdeclast also adds round key, so supply zero. */
    vi = _mm_aesdeclast_si128(vi, _mm_setzero_si128());

    _mm_storeu_si128((__m128i_u *)o, vi);
    return true;
}

static bool test_IMC(uint8_t *o, const uint8_t *i)
{
    __m128i vi = _mm_loadu_si128((const __m128i_u *)i);

    vi = _mm_aesimc_si128(vi);

    _mm_storeu_si128((__m128i_u *)o, vi);
    return true;
}

static bool test_ISB_ISR_AK_IMC(uint8_t *o, const uint8_t *i, const uint8_t *k)
{
    return false;
}

static bool test_ISB_ISR_IMC_AK(uint8_t *o, const uint8_t *i, const uint8_t *k)
{
    __m128i vi = _mm_loadu_si128((const __m128i_u *)i);
    __m128i vk = _mm_loadu_si128((const __m128i_u *)k);

    vi = _mm_aesdec_si128(vi, vk);

    _mm_storeu_si128((__m128i_u *)o, vi);
    return true;
}
