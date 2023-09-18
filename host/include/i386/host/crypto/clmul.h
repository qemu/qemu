/*
 * x86 specific clmul acceleration.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef X86_HOST_CRYPTO_CLMUL_H
#define X86_HOST_CRYPTO_CLMUL_H

#include "host/cpuinfo.h"
#include <immintrin.h>

#if defined(__PCLMUL__)
# define HAVE_CLMUL_ACCEL  true
# define ATTR_CLMUL_ACCEL
#else
# define HAVE_CLMUL_ACCEL  likely(cpuinfo & CPUINFO_PCLMUL)
# define ATTR_CLMUL_ACCEL  __attribute__((target("pclmul")))
#endif

static inline Int128 ATTR_CLMUL_ACCEL
clmul_64_accel(uint64_t n, uint64_t m)
{
    union { __m128i v; Int128 s; } u;

    u.v = _mm_clmulepi64_si128(_mm_set_epi64x(0, n), _mm_set_epi64x(0, m), 0);
    return u.s;
}

#endif /* X86_HOST_CRYPTO_CLMUL_H */
