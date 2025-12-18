/*
 * Power v2.07 specific aes acceleration.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PPC_HOST_CRYPTO_AES_ROUND_H
#define PPC_HOST_CRYPTO_AES_ROUND_H

#ifdef __ALTIVEC__
#include "host/cpuinfo.h"

#ifdef __CRYPTO__
# define HAVE_AES_ACCEL  true
#else
# define HAVE_AES_ACCEL  likely(cpuinfo & CPUINFO_CRYPTO)
#endif
#define ATTR_AES_ACCEL

/*
 * While there is <altivec.h>, both gcc and clang "aid" with the
 * endianness issues in different ways. Just use inline asm instead.
 */

/* Bytes in memory are host-endian; bytes in register are @be. */
static inline AESStateVec aes_accel_ld(const AESState *p, bool be)
{
    AESStateVec r;

    if (be) {
        asm("lvx %0, 0, %1" : "=v"(r) : "r"(p), "m"(*p));
    } else if (HOST_BIG_ENDIAN) {
        AESStateVec rev = {
            15, 14, 13, 12, 11, 10, 9, 8, 7,  6,  5,  4,  3,  2,  1,  0,
        };
        asm("lvx %0, 0, %1\n\t"
            "vperm %0, %0, %0, %2"
            : "=v"(r) : "r"(p), "v"(rev), "m"(*p));
    } else {
#ifdef __POWER9_VECTOR__
        asm("lxvb16x %x0, 0, %1" : "=v"(r) : "r"(p), "m"(*p));
#else
        asm("lxvd2x %x0, 0, %1\n\t"
            "xxpermdi %x0, %x0, %x0, 2"
            : "=v"(r) : "r"(p), "m"(*p));
#endif
    }
    return r;
}

static void aes_accel_st(AESState *p, AESStateVec r, bool be)
{
    if (be) {
        asm("stvx %1, 0, %2" : "=m"(*p) : "v"(r), "r"(p));
    } else if (HOST_BIG_ENDIAN) {
        AESStateVec rev = {
            15, 14, 13, 12, 11, 10, 9, 8, 7,  6,  5,  4,  3,  2,  1,  0,
        };
        asm("vperm %1, %1, %1, %2\n\t"
            "stvx %1, 0, %3"
            : "=m"(*p), "+v"(r) : "v"(rev), "r"(p));
    } else {
#ifdef __POWER9_VECTOR__
        asm("stxvb16x %x1, 0, %2" : "=m"(*p) : "v"(r), "r"(p));
#else
        asm("xxpermdi %x1, %x1, %x1, 2\n\t"
            "stxvd2x %x1, 0, %2"
            : "=m"(*p), "+v"(r) : "r"(p));
#endif
    }
}

static inline AESStateVec aes_accel_vcipher(AESStateVec d, AESStateVec k)
{
    asm("vcipher %0, %0, %1" : "+v"(d) : "v"(k));
    return d;
}

static inline AESStateVec aes_accel_vncipher(AESStateVec d, AESStateVec k)
{
    asm("vncipher %0, %0, %1" : "+v"(d) : "v"(k));
    return d;
}

static inline AESStateVec aes_accel_vcipherlast(AESStateVec d, AESStateVec k)
{
    asm("vcipherlast %0, %0, %1" : "+v"(d) : "v"(k));
    return d;
}

static inline AESStateVec aes_accel_vncipherlast(AESStateVec d, AESStateVec k)
{
    asm("vncipherlast %0, %0, %1" : "+v"(d) : "v"(k));
    return d;
}

static inline void
aesenc_MC_accel(AESState *ret, const AESState *st, bool be)
{
    AESStateVec t, z = { };

    t = aes_accel_ld(st, be);
    t = aes_accel_vncipherlast(t, z);
    t = aes_accel_vcipher(t, z);
    aes_accel_st(ret, t, be);
}

static inline void
aesenc_SB_SR_AK_accel(AESState *ret, const AESState *st,
                      const AESState *rk, bool be)
{
    AESStateVec t, k;

    t = aes_accel_ld(st, be);
    k = aes_accel_ld(rk, be);
    t = aes_accel_vcipherlast(t, k);
    aes_accel_st(ret, t, be);
}

static inline void
aesenc_SB_SR_MC_AK_accel(AESState *ret, const AESState *st,
                         const AESState *rk, bool be)
{
    AESStateVec t, k;

    t = aes_accel_ld(st, be);
    k = aes_accel_ld(rk, be);
    t = aes_accel_vcipher(t, k);
    aes_accel_st(ret, t, be);
}

static inline void
aesdec_IMC_accel(AESState *ret, const AESState *st, bool be)
{
    AESStateVec t, z = { };

    t = aes_accel_ld(st, be);
    t = aes_accel_vcipherlast(t, z);
    t = aes_accel_vncipher(t, z);
    aes_accel_st(ret, t, be);
}

static inline void
aesdec_ISB_ISR_AK_accel(AESState *ret, const AESState *st,
                        const AESState *rk, bool be)
{
    AESStateVec t, k;

    t = aes_accel_ld(st, be);
    k = aes_accel_ld(rk, be);
    t = aes_accel_vncipherlast(t, k);
    aes_accel_st(ret, t, be);
}

static inline void
aesdec_ISB_ISR_AK_IMC_accel(AESState *ret, const AESState *st,
                            const AESState *rk, bool be)
{
    AESStateVec t, k;

    t = aes_accel_ld(st, be);
    k = aes_accel_ld(rk, be);
    t = aes_accel_vncipher(t, k);
    aes_accel_st(ret, t, be);
}

static inline void
aesdec_ISB_ISR_IMC_AK_accel(AESState *ret, const AESState *st,
                            const AESState *rk, bool be)
{
    AESStateVec t, k, z = { };

    t = aes_accel_ld(st, be);
    k = aes_accel_ld(rk, be);
    t = aes_accel_vncipher(t, z);
    aes_accel_st(ret, t ^ k, be);
}
#else
/* Without ALTIVEC, we can't even write inline assembly. */
#include "host/include/generic/host/crypto/aes-round.h"
#endif

#endif /* PPC_HOST_CRYPTO_AES_ROUND_H */
