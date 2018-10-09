/*
 * QEMU Crypto XTS cipher mode
 *
 * Copyright (c) 2015-2016 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * This code is originally derived from public domain / WTFPL code in
 * LibTomCrypt crytographic library http://libtom.org. The XTS code
 * was donated by Elliptic Semiconductor Inc (www.ellipticsemi.com)
 * to the LibTom Projects
 *
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "crypto/xts.h"

typedef union {
    uint8_t b[XTS_BLOCK_SIZE];
    uint64_t u[2];
} xts_uint128;

static inline void xts_uint128_xor(xts_uint128 *D,
                                   const xts_uint128 *S1,
                                   const xts_uint128 *S2)
{
    D->u[0] = S1->u[0] ^ S2->u[0];
    D->u[1] = S1->u[1] ^ S2->u[1];
}

static inline void xts_uint128_cpu_to_les(xts_uint128 *v)
{
    cpu_to_le64s(&v->u[0]);
    cpu_to_le64s(&v->u[1]);
}

static inline void xts_uint128_le_to_cpus(xts_uint128 *v)
{
    le64_to_cpus(&v->u[0]);
    le64_to_cpus(&v->u[1]);
}

static void xts_mult_x(xts_uint128 *I)
{
    uint64_t tt;

    xts_uint128_le_to_cpus(I);

    tt = I->u[0] >> 63;
    I->u[0] <<= 1;

    if (I->u[1] >> 63) {
        I->u[0] ^= 0x87;
    }
    I->u[1] <<= 1;
    I->u[1] |= tt;

    xts_uint128_cpu_to_les(I);
}


/**
 * xts_tweak_encdec:
 * @param ctxt: the cipher context
 * @param func: the cipher function
 * @src: buffer providing the input text of XTS_BLOCK_SIZE bytes
 * @dst: buffer to output the output text of XTS_BLOCK_SIZE bytes
 * @iv: the initialization vector tweak of XTS_BLOCK_SIZE bytes
 *
 * Encrypt/decrypt data with a tweak
 */
static inline void xts_tweak_encdec(const void *ctx,
                                    xts_cipher_func *func,
                                    const xts_uint128 *src,
                                    xts_uint128 *dst,
                                    xts_uint128 *iv)
{
    /* tweak encrypt block i */
    xts_uint128_xor(dst, src, iv);

    func(ctx, XTS_BLOCK_SIZE, dst->b, dst->b);

    xts_uint128_xor(dst, dst, iv);

    /* LFSR the tweak */
    xts_mult_x(iv);
}


void xts_decrypt(const void *datactx,
                 const void *tweakctx,
                 xts_cipher_func *encfunc,
                 xts_cipher_func *decfunc,
                 uint8_t *iv,
                 size_t length,
                 uint8_t *dst,
                 const uint8_t *src)
{
    xts_uint128 PP, CC, T;
    unsigned long i, m, mo, lim;

    /* get number of blocks */
    m = length >> 4;
    mo = length & 15;

    /* must have at least one full block */
    g_assert(m != 0);

    if (mo == 0) {
        lim = m;
    } else {
        lim = m - 1;
    }

    /* encrypt the iv */
    encfunc(tweakctx, XTS_BLOCK_SIZE, T.b, iv);

    if (QEMU_PTR_IS_ALIGNED(src, sizeof(uint64_t)) &&
        QEMU_PTR_IS_ALIGNED(dst, sizeof(uint64_t))) {
        xts_uint128 *S = (xts_uint128 *)src;
        xts_uint128 *D = (xts_uint128 *)dst;
        for (i = 0; i < lim; i++, S++, D++) {
            xts_tweak_encdec(datactx, decfunc, S, D, &T);
        }
    } else {
        xts_uint128 D;

        for (i = 0; i < lim; i++) {
            memcpy(&D, src, XTS_BLOCK_SIZE);
            xts_tweak_encdec(datactx, decfunc, &D, &D, &T);
            memcpy(dst, &D, XTS_BLOCK_SIZE);
            src += XTS_BLOCK_SIZE;
            dst += XTS_BLOCK_SIZE;
        }
    }

    /* if length is not a multiple of XTS_BLOCK_SIZE then */
    if (mo > 0) {
        xts_uint128 S, D;
        memcpy(&CC, &T, XTS_BLOCK_SIZE);
        xts_mult_x(&CC);

        /* PP = tweak decrypt block m-1 */
        memcpy(&S, src, XTS_BLOCK_SIZE);
        xts_tweak_encdec(datactx, decfunc, &S, &PP, &CC);

        /* Pm = first length % XTS_BLOCK_SIZE bytes of PP */
        for (i = 0; i < mo; i++) {
            CC.b[i] = src[XTS_BLOCK_SIZE + i];
            dst[XTS_BLOCK_SIZE + i] = PP.b[i];
        }
        for (; i < XTS_BLOCK_SIZE; i++) {
            CC.b[i] = PP.b[i];
        }

        /* Pm-1 = Tweak uncrypt CC */
        xts_tweak_encdec(datactx, decfunc, &CC, &D, &T);
        memcpy(dst, &D, XTS_BLOCK_SIZE);
    }

    /* Decrypt the iv back */
    decfunc(tweakctx, XTS_BLOCK_SIZE, iv, T.b);
}


void xts_encrypt(const void *datactx,
                 const void *tweakctx,
                 xts_cipher_func *encfunc,
                 xts_cipher_func *decfunc,
                 uint8_t *iv,
                 size_t length,
                 uint8_t *dst,
                 const uint8_t *src)
{
    xts_uint128 PP, CC, T;
    unsigned long i, m, mo, lim;

    /* get number of blocks */
    m = length >> 4;
    mo = length & 15;

    /* must have at least one full block */
    g_assert(m != 0);

    if (mo == 0) {
        lim = m;
    } else {
        lim = m - 1;
    }

    /* encrypt the iv */
    encfunc(tweakctx, XTS_BLOCK_SIZE, T.b, iv);

    if (QEMU_PTR_IS_ALIGNED(src, sizeof(uint64_t)) &&
        QEMU_PTR_IS_ALIGNED(dst, sizeof(uint64_t))) {
        xts_uint128 *S = (xts_uint128 *)src;
        xts_uint128 *D = (xts_uint128 *)dst;
        for (i = 0; i < lim; i++, S++, D++) {
            xts_tweak_encdec(datactx, encfunc, S, D, &T);
        }
    } else {
        xts_uint128 D;

        for (i = 0; i < lim; i++) {
            memcpy(&D, src, XTS_BLOCK_SIZE);
            xts_tweak_encdec(datactx, encfunc, &D, &D, &T);
            memcpy(dst, &D, XTS_BLOCK_SIZE);

            dst += XTS_BLOCK_SIZE;
            src += XTS_BLOCK_SIZE;
        }
    }

    /* if length is not a multiple of XTS_BLOCK_SIZE then */
    if (mo > 0) {
        xts_uint128 S, D;
        /* CC = tweak encrypt block m-1 */
        memcpy(&S, src, XTS_BLOCK_SIZE);
        xts_tweak_encdec(datactx, encfunc, &S, &CC, &T);

        /* Cm = first length % XTS_BLOCK_SIZE bytes of CC */
        for (i = 0; i < mo; i++) {
            PP.b[i] = src[XTS_BLOCK_SIZE + i];
            dst[XTS_BLOCK_SIZE + i] = CC.b[i];
        }

        for (; i < XTS_BLOCK_SIZE; i++) {
            PP.b[i] = CC.b[i];
        }

        /* Cm-1 = Tweak encrypt PP */
        xts_tweak_encdec(datactx, encfunc, &PP, &D, &T);
        memcpy(dst, &D, XTS_BLOCK_SIZE);
    }

    /* Decrypt the iv back */
    decfunc(tweakctx, XTS_BLOCK_SIZE, iv, T.b);
}
