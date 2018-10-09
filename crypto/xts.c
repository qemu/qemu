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
#include "crypto/xts.h"

typedef union {
    uint8_t b[XTS_BLOCK_SIZE];
    uint64_t u[2];
} xts_uint128;


static void xts_mult_x(uint8_t *I)
{
    int x;
    uint8_t t, tt;

    for (x = t = 0; x < 16; x++) {
        tt = I[x] >> 7;
        I[x] = ((I[x] << 1) | t) & 0xFF;
        t = tt;
    }
    if (tt) {
        I[0] ^= 0x87;
    }
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
static void xts_tweak_encdec(const void *ctx,
                             xts_cipher_func *func,
                             const uint8_t *src,
                             uint8_t *dst,
                             uint8_t *iv)
{
    unsigned long x;

    /* tweak encrypt block i */
    for (x = 0; x < XTS_BLOCK_SIZE; x++) {
        dst[x] = src[x] ^ iv[x];
    }

    func(ctx, XTS_BLOCK_SIZE, dst, dst);

    for (x = 0; x < XTS_BLOCK_SIZE; x++) {
        dst[x] = dst[x] ^ iv[x];
    }

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

    for (i = 0; i < lim; i++) {
        xts_tweak_encdec(datactx, decfunc, src, dst, T.b);

        src += XTS_BLOCK_SIZE;
        dst += XTS_BLOCK_SIZE;
    }

    /* if length is not a multiple of XTS_BLOCK_SIZE then */
    if (mo > 0) {
        memcpy(&CC, &T, XTS_BLOCK_SIZE);
        xts_mult_x(CC.b);

        /* PP = tweak decrypt block m-1 */
        xts_tweak_encdec(datactx, decfunc, src, PP.b, CC.b);

        /* Pm = first length % XTS_BLOCK_SIZE bytes of PP */
        for (i = 0; i < mo; i++) {
            CC.b[i] = src[XTS_BLOCK_SIZE + i];
            dst[XTS_BLOCK_SIZE + i] = PP.b[i];
        }
        for (; i < XTS_BLOCK_SIZE; i++) {
            CC.b[i] = PP.b[i];
        }

        /* Pm-1 = Tweak uncrypt CC */
        xts_tweak_encdec(datactx, decfunc, CC.b, dst, T.b);
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

    for (i = 0; i < lim; i++) {
        xts_tweak_encdec(datactx, encfunc, src, dst, T.b);

        dst += XTS_BLOCK_SIZE;
        src += XTS_BLOCK_SIZE;
    }

    /* if length is not a multiple of XTS_BLOCK_SIZE then */
    if (mo > 0) {
        /* CC = tweak encrypt block m-1 */
        xts_tweak_encdec(datactx, encfunc, src, CC.b, T.b);

        /* Cm = first length % XTS_BLOCK_SIZE bytes of CC */
        for (i = 0; i < mo; i++) {
            PP.b[i] = src[XTS_BLOCK_SIZE + i];
            dst[XTS_BLOCK_SIZE + i] = CC.b[i];
        }

        for (; i < XTS_BLOCK_SIZE; i++) {
            PP.b[i] = CC.b[i];
        }

        /* Cm-1 = Tweak encrypt PP */
        xts_tweak_encdec(datactx, encfunc, PP.b, dst, T.b);
    }

    /* Decrypt the iv back */
    decfunc(tweakctx, XTS_BLOCK_SIZE, iv, T.b);
}
