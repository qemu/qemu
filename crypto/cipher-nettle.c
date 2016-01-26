/*
 * QEMU Crypto cipher nettle algorithms
 *
 * Copyright (c) 2015 Red Hat, Inc.
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
 */

#include "qemu/osdep.h"
#include <nettle/nettle-types.h>
#include <nettle/aes.h>
#include <nettle/des.h>
#include <nettle/cbc.h>

#if CONFIG_NETTLE_VERSION_MAJOR < 3
typedef nettle_crypt_func nettle_cipher_func;

typedef void *       cipher_ctx_t;
typedef unsigned     cipher_length_t;
#else
typedef const void * cipher_ctx_t;
typedef size_t       cipher_length_t;
#endif

static nettle_cipher_func aes_encrypt_wrapper;
static nettle_cipher_func aes_decrypt_wrapper;
static nettle_cipher_func des_encrypt_wrapper;
static nettle_cipher_func des_decrypt_wrapper;

static void aes_encrypt_wrapper(cipher_ctx_t ctx, cipher_length_t length,
                                uint8_t *dst, const uint8_t *src)
{
    aes_encrypt(ctx, length, dst, src);
}

static void aes_decrypt_wrapper(cipher_ctx_t ctx, cipher_length_t length,
                                uint8_t *dst, const uint8_t *src)
{
    aes_decrypt(ctx, length, dst, src);
}

static void des_encrypt_wrapper(cipher_ctx_t ctx, cipher_length_t length,
                                uint8_t *dst, const uint8_t *src)
{
    des_encrypt(ctx, length, dst, src);
}

static void des_decrypt_wrapper(cipher_ctx_t ctx, cipher_length_t length,
                                uint8_t *dst, const uint8_t *src)
{
    des_decrypt(ctx, length, dst, src);
}

typedef struct QCryptoCipherNettle QCryptoCipherNettle;
struct QCryptoCipherNettle {
    void *ctx_encrypt;
    void *ctx_decrypt;
    nettle_cipher_func *alg_encrypt;
    nettle_cipher_func *alg_decrypt;
    uint8_t *iv;
    size_t blocksize;
};

bool qcrypto_cipher_supports(QCryptoCipherAlgorithm alg)
{
    switch (alg) {
    case QCRYPTO_CIPHER_ALG_DES_RFB:
    case QCRYPTO_CIPHER_ALG_AES_128:
    case QCRYPTO_CIPHER_ALG_AES_192:
    case QCRYPTO_CIPHER_ALG_AES_256:
        return true;
    default:
        return false;
    }
}


QCryptoCipher *qcrypto_cipher_new(QCryptoCipherAlgorithm alg,
                                  QCryptoCipherMode mode,
                                  const uint8_t *key, size_t nkey,
                                  Error **errp)
{
    QCryptoCipher *cipher;
    QCryptoCipherNettle *ctx;
    uint8_t *rfbkey;

    switch (mode) {
    case QCRYPTO_CIPHER_MODE_ECB:
    case QCRYPTO_CIPHER_MODE_CBC:
        break;
    default:
        error_setg(errp, "Unsupported cipher mode %d", mode);
        return NULL;
    }

    if (!qcrypto_cipher_validate_key_length(alg, nkey, errp)) {
        return NULL;
    }

    cipher = g_new0(QCryptoCipher, 1);
    cipher->alg = alg;
    cipher->mode = mode;

    ctx = g_new0(QCryptoCipherNettle, 1);

    switch (alg) {
    case QCRYPTO_CIPHER_ALG_DES_RFB:
        ctx->ctx_encrypt = g_new0(struct des_ctx, 1);
        ctx->ctx_decrypt = NULL; /* 1 ctx can do both */
        rfbkey = qcrypto_cipher_munge_des_rfb_key(key, nkey);
        des_set_key(ctx->ctx_encrypt, rfbkey);
        g_free(rfbkey);

        ctx->alg_encrypt = des_encrypt_wrapper;
        ctx->alg_decrypt = des_decrypt_wrapper;

        ctx->blocksize = DES_BLOCK_SIZE;
        break;

    case QCRYPTO_CIPHER_ALG_AES_128:
    case QCRYPTO_CIPHER_ALG_AES_192:
    case QCRYPTO_CIPHER_ALG_AES_256:
        ctx->ctx_encrypt = g_new0(struct aes_ctx, 1);
        ctx->ctx_decrypt = g_new0(struct aes_ctx, 1);

        aes_set_encrypt_key(ctx->ctx_encrypt, nkey, key);
        aes_set_decrypt_key(ctx->ctx_decrypt, nkey, key);

        ctx->alg_encrypt = aes_encrypt_wrapper;
        ctx->alg_decrypt = aes_decrypt_wrapper;

        ctx->blocksize = AES_BLOCK_SIZE;
        break;
    default:
        error_setg(errp, "Unsupported cipher algorithm %d", alg);
        goto error;
    }

    ctx->iv = g_new0(uint8_t, ctx->blocksize);
    cipher->opaque = ctx;

    return cipher;

 error:
    g_free(cipher);
    g_free(ctx);
    return NULL;
}


void qcrypto_cipher_free(QCryptoCipher *cipher)
{
    QCryptoCipherNettle *ctx;

    if (!cipher) {
        return;
    }

    ctx = cipher->opaque;
    g_free(ctx->iv);
    g_free(ctx->ctx_encrypt);
    g_free(ctx->ctx_decrypt);
    g_free(ctx);
    g_free(cipher);
}


int qcrypto_cipher_encrypt(QCryptoCipher *cipher,
                           const void *in,
                           void *out,
                           size_t len,
                           Error **errp)
{
    QCryptoCipherNettle *ctx = cipher->opaque;

    if (len % ctx->blocksize) {
        error_setg(errp, "Length %zu must be a multiple of block size %zu",
                   len, ctx->blocksize);
        return -1;
    }

    switch (cipher->mode) {
    case QCRYPTO_CIPHER_MODE_ECB:
        ctx->alg_encrypt(ctx->ctx_encrypt, len, out, in);
        break;

    case QCRYPTO_CIPHER_MODE_CBC:
        cbc_encrypt(ctx->ctx_encrypt, ctx->alg_encrypt,
                    ctx->blocksize, ctx->iv,
                    len, out, in);
        break;
    default:
        error_setg(errp, "Unsupported cipher algorithm %d",
                   cipher->alg);
        return -1;
    }
    return 0;
}


int qcrypto_cipher_decrypt(QCryptoCipher *cipher,
                           const void *in,
                           void *out,
                           size_t len,
                           Error **errp)
{
    QCryptoCipherNettle *ctx = cipher->opaque;

    if (len % ctx->blocksize) {
        error_setg(errp, "Length %zu must be a multiple of block size %zu",
                   len, ctx->blocksize);
        return -1;
    }

    switch (cipher->mode) {
    case QCRYPTO_CIPHER_MODE_ECB:
        ctx->alg_decrypt(ctx->ctx_decrypt ? ctx->ctx_decrypt : ctx->ctx_encrypt,
                         len, out, in);
        break;

    case QCRYPTO_CIPHER_MODE_CBC:
        cbc_decrypt(ctx->ctx_decrypt ? ctx->ctx_decrypt : ctx->ctx_encrypt,
                    ctx->alg_decrypt, ctx->blocksize, ctx->iv,
                    len, out, in);
        break;
    default:
        error_setg(errp, "Unsupported cipher algorithm %d",
                   cipher->alg);
        return -1;
    }
    return 0;
}

int qcrypto_cipher_setiv(QCryptoCipher *cipher,
                         const uint8_t *iv, size_t niv,
                         Error **errp)
{
    QCryptoCipherNettle *ctx = cipher->opaque;
    if (niv != ctx->blocksize) {
        error_setg(errp, "Expected IV size %zu not %zu",
                   ctx->blocksize, niv);
        return -1;
    }
    memcpy(ctx->iv, iv, niv);
    return 0;
}
