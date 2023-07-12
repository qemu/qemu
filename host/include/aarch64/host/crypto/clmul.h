/*
 * AArch64 specific clmul acceleration.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef AARCH64_HOST_CRYPTO_CLMUL_H
#define AARCH64_HOST_CRYPTO_CLMUL_H

#include "host/cpuinfo.h"
#include <arm_neon.h>

/*
 * 64x64->128 pmull is available with FEAT_PMULL.
 * Both FEAT_AES and FEAT_PMULL are covered under the same macro.
 */
#ifdef __ARM_FEATURE_AES
# define HAVE_CLMUL_ACCEL  true
#else
# define HAVE_CLMUL_ACCEL  likely(cpuinfo & CPUINFO_PMULL)
#endif
#if !defined(__ARM_FEATURE_AES) && defined(CONFIG_ARM_AES_BUILTIN)
# define ATTR_CLMUL_ACCEL  __attribute__((target("+crypto")))
#else
# define ATTR_CLMUL_ACCEL
#endif

static inline Int128 ATTR_CLMUL_ACCEL
clmul_64_accel(uint64_t n, uint64_t m)
{
    union { poly128_t v; Int128 s; } u;

#ifdef CONFIG_ARM_AES_BUILTIN
    u.v = vmull_p64((poly64_t)n, (poly64_t)m);
#else
    asm(".arch_extension aes\n\t"
        "pmull %0.1q, %1.1d, %2.1d" : "=w"(u.v) : "w"(n), "w"(m));
#endif
    return u.s;
}

#endif /* AARCH64_HOST_CRYPTO_CLMUL_H */
