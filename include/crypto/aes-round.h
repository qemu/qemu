/*
 * AES round fragments, generic version
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2023 Linaro, Ltd.
 */

#ifndef CRYPTO_AES_ROUND_H
#define CRYPTO_AES_ROUND_H

/* Hosts with acceleration will usually need a 16-byte vector type. */
typedef uint8_t AESStateVec __attribute__((vector_size(16)));

typedef union {
    uint8_t b[16];
    uint32_t w[4];
    uint64_t d[2];
    AESStateVec v;
} AESState;

#include "host/crypto/aes-round.h"

/*
 * Perform SubBytes + ShiftRows + AddRoundKey.
 */

void aesenc_SB_SR_AK_gen(AESState *ret, const AESState *st,
                         const AESState *rk);
void aesenc_SB_SR_AK_genrev(AESState *ret, const AESState *st,
                            const AESState *rk);

static inline void aesenc_SB_SR_AK(AESState *r, const AESState *st,
                                   const AESState *rk, bool be)
{
    if (HAVE_AES_ACCEL) {
        aesenc_SB_SR_AK_accel(r, st, rk, be);
    } else if (HOST_BIG_ENDIAN == be) {
        aesenc_SB_SR_AK_gen(r, st, rk);
    } else {
        aesenc_SB_SR_AK_genrev(r, st, rk);
    }
}

#endif /* CRYPTO_AES_ROUND_H */
