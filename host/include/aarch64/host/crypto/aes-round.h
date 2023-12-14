/*
 * AArch64 specific aes acceleration.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef AARCH64_HOST_CRYPTO_AES_ROUND_H
#define AARCH64_HOST_CRYPTO_AES_ROUND_H

#include "host/cpuinfo.h"
#include <arm_neon.h>

#ifdef __ARM_FEATURE_AES
# define HAVE_AES_ACCEL  true
#else
# define HAVE_AES_ACCEL  likely(cpuinfo & CPUINFO_AES)
#endif
#if !defined(__ARM_FEATURE_AES) && defined(CONFIG_ARM_AES_BUILTIN)
# define ATTR_AES_ACCEL  __attribute__((target("+crypto")))
#else
# define ATTR_AES_ACCEL
#endif

static inline uint8x16_t aes_accel_bswap(uint8x16_t x)
{
    return vqtbl1q_u8(x, (uint8x16_t){ 15, 14, 13, 12, 11, 10, 9, 8,
                                        7,  6,  5,  4,  3,  2, 1, 0, });
}

#ifdef CONFIG_ARM_AES_BUILTIN
# define aes_accel_aesd            vaesdq_u8
# define aes_accel_aese            vaeseq_u8
# define aes_accel_aesmc           vaesmcq_u8
# define aes_accel_aesimc          vaesimcq_u8
# define aes_accel_aesd_imc(S, K)  vaesimcq_u8(vaesdq_u8(S, K))
# define aes_accel_aese_mc(S, K)   vaesmcq_u8(vaeseq_u8(S, K))
#else
static inline uint8x16_t aes_accel_aesd(uint8x16_t d, uint8x16_t k)
{
    asm(".arch_extension aes\n\t"
        "aesd %0.16b, %1.16b" : "+w"(d) : "w"(k));
    return d;
}

static inline uint8x16_t aes_accel_aese(uint8x16_t d, uint8x16_t k)
{
    asm(".arch_extension aes\n\t"
        "aese %0.16b, %1.16b" : "+w"(d) : "w"(k));
    return d;
}

static inline uint8x16_t aes_accel_aesmc(uint8x16_t d)
{
    asm(".arch_extension aes\n\t"
        "aesmc %0.16b, %1.16b" : "=w"(d) : "w"(d));
    return d;
}

static inline uint8x16_t aes_accel_aesimc(uint8x16_t d)
{
    asm(".arch_extension aes\n\t"
        "aesimc %0.16b, %1.16b" : "=w"(d) : "w"(d));
    return d;
}

/* Most CPUs fuse AESD+AESIMC in the execution pipeline. */
static inline uint8x16_t aes_accel_aesd_imc(uint8x16_t d, uint8x16_t k)
{
    asm(".arch_extension aes\n\t"
        "aesd %0.16b, %1.16b\n\t"
        "aesimc %0.16b, %0.16b" : "+w"(d) : "w"(k));
    return d;
}

/* Most CPUs fuse AESE+AESMC in the execution pipeline. */
static inline uint8x16_t aes_accel_aese_mc(uint8x16_t d, uint8x16_t k)
{
    asm(".arch_extension aes\n\t"
        "aese %0.16b, %1.16b\n\t"
        "aesmc %0.16b, %0.16b" : "+w"(d) : "w"(k));
    return d;
}
#endif /* CONFIG_ARM_AES_BUILTIN */

static inline void ATTR_AES_ACCEL
aesenc_MC_accel(AESState *ret, const AESState *st, bool be)
{
    uint8x16_t t = (uint8x16_t)st->v;

    if (be) {
        t = aes_accel_bswap(t);
        t = aes_accel_aesmc(t);
        t = aes_accel_bswap(t);
    } else {
        t = aes_accel_aesmc(t);
    }
    ret->v = (AESStateVec)t;
}

static inline void ATTR_AES_ACCEL
aesenc_SB_SR_AK_accel(AESState *ret, const AESState *st,
                      const AESState *rk, bool be)
{
    uint8x16_t t = (uint8x16_t)st->v;
    uint8x16_t z = { };

    if (be) {
        t = aes_accel_bswap(t);
        t = aes_accel_aese(t, z);
        t = aes_accel_bswap(t);
    } else {
        t = aes_accel_aese(t, z);
    }
    ret->v = (AESStateVec)t ^ rk->v;
}

static inline void ATTR_AES_ACCEL
aesenc_SB_SR_MC_AK_accel(AESState *ret, const AESState *st,
                         const AESState *rk, bool be)
{
    uint8x16_t t = (uint8x16_t)st->v;
    uint8x16_t z = { };

    if (be) {
        t = aes_accel_bswap(t);
        t = aes_accel_aese_mc(t, z);
        t = aes_accel_bswap(t);
    } else {
        t = aes_accel_aese_mc(t, z);
    }
    ret->v = (AESStateVec)t ^ rk->v;
}

static inline void ATTR_AES_ACCEL
aesdec_IMC_accel(AESState *ret, const AESState *st, bool be)
{
    uint8x16_t t = (uint8x16_t)st->v;

    if (be) {
        t = aes_accel_bswap(t);
        t = aes_accel_aesimc(t);
        t = aes_accel_bswap(t);
    } else {
        t = aes_accel_aesimc(t);
    }
    ret->v = (AESStateVec)t;
}

static inline void ATTR_AES_ACCEL
aesdec_ISB_ISR_AK_accel(AESState *ret, const AESState *st,
                        const AESState *rk, bool be)
{
    uint8x16_t t = (uint8x16_t)st->v;
    uint8x16_t z = { };

    if (be) {
        t = aes_accel_bswap(t);
        t = aes_accel_aesd(t, z);
        t = aes_accel_bswap(t);
    } else {
        t = aes_accel_aesd(t, z);
    }
    ret->v = (AESStateVec)t ^ rk->v;
}

static inline void ATTR_AES_ACCEL
aesdec_ISB_ISR_AK_IMC_accel(AESState *ret, const AESState *st,
                            const AESState *rk, bool be)
{
    uint8x16_t t = (uint8x16_t)st->v;
    uint8x16_t k = (uint8x16_t)rk->v;
    uint8x16_t z = { };

    if (be) {
        t = aes_accel_bswap(t);
        k = aes_accel_bswap(k);
        t = aes_accel_aesd(t, z);
        t ^= k;
        t = aes_accel_aesimc(t);
        t = aes_accel_bswap(t);
    } else {
        t = aes_accel_aesd(t, z);
        t ^= k;
        t = aes_accel_aesimc(t);
    }
    ret->v = (AESStateVec)t;
}

static inline void ATTR_AES_ACCEL
aesdec_ISB_ISR_IMC_AK_accel(AESState *ret, const AESState *st,
                            const AESState *rk, bool be)
{
    uint8x16_t t = (uint8x16_t)st->v;
    uint8x16_t z = { };

    if (be) {
        t = aes_accel_bswap(t);
        t = aes_accel_aesd_imc(t, z);
        t = aes_accel_bswap(t);
    } else {
        t = aes_accel_aesd_imc(t, z);
    }
    ret->v = (AESStateVec)t ^ rk->v;
}

#endif /* AARCH64_HOST_CRYPTO_AES_ROUND_H */
