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
#include "crypto/xts.h"

#include <nettle/nettle-types.h>
#include <nettle/aes.h>
#include <nettle/des.h>
#include <nettle/cbc.h>
#include <nettle/cast128.h>
#include <nettle/serpent.h>
#include <nettle/twofish.h>

#if CONFIG_NETTLE_VERSION_MAJOR < 3
typedef nettle_crypt_func nettle_cipher_func;

typedef void *       cipher_ctx_t;
typedef unsigned     cipher_length_t;

#define cast5_set_key cast128_set_key
#else
typedef const void * cipher_ctx_t;
typedef size_t       cipher_length_t;
#endif

static nettle_cipher_func aes_encrypt_wrapper;
static nettle_cipher_func aes_decrypt_wrapper;
static nettle_cipher_func des_encrypt_wrapper;
static nettle_cipher_func des_decrypt_wrapper;

typedef struct QCryptoNettleAES {
    struct aes_ctx enc;
    struct aes_ctx dec;
} QCryptoNettleAES;

static void aes_encrypt_wrapper(cipher_ctx_t ctx, cipher_length_t length,
                                uint8_t *dst, const uint8_t *src)
{
    const QCryptoNettleAES *aesctx = ctx;
    aes_encrypt(&aesctx->enc, length, dst, src);
}

static void aes_decrypt_wrapper(cipher_ctx_t ctx, cipher_length_t length,
                                uint8_t *dst, const uint8_t *src)
{
    const QCryptoNettleAES *aesctx = ctx;
    aes_decrypt(&aesctx->dec, length, dst, src);
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

static void cast128_encrypt_wrapper(cipher_ctx_t ctx, cipher_length_t length,
                                    uint8_t *dst, const uint8_t *src)
{
    cast128_encrypt(ctx, length, dst, src);
}

static void cast128_decrypt_wrapper(cipher_ctx_t ctx, cipher_length_t length,
                                    uint8_t *dst, const uint8_t *src)
{
    cast128_decrypt(ctx, length, dst, src);
}

static void serpent_encrypt_wrapper(cipher_ctx_t ctx, cipher_length_t length,
                                    uint8_t *dst, const uint8_t *src)
{
    serpent_encrypt(ctx, length, dst, src);
}

static void serpent_decrypt_wrapper(cipher_ctx_t ctx, cipher_length_t length,
                                    uint8_t *dst, const uint8_t *src)
{
    serpent_decrypt(ctx, length, dst, src);
}

static void twofish_encrypt_wrapper(cipher_ctx_t ctx, cipher_length_t length,
                                    uint8_t *dst, const uint8_t *src)
{
    twofish_encrypt(ctx, length, dst, src);
}

static void twofish_decrypt_wrapper(cipher_ctx_t ctx, cipher_length_t length,
                                    uint8_t *dst, const uint8_t *src)
{
    twofish_decrypt(ctx, length, dst, src);
}

typedef struct QCryptoCipherNettle QCryptoCipherNettle;
struct QCryptoCipherNettle {
    /* Primary cipher context for all modes */
    void *ctx;
    /* Second cipher context for XTS mode only */
    void *ctx_tweak;
    /* Cipher callbacks for both contexts */
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
    case QCRYPTO_CIPHER_ALG_CAST5_128:
    case QCRYPTO_CIPHER_ALG_SERPENT_128:
    case QCRYPTO_CIPHER_ALG_SERPENT_192:
    case QCRYPTO_CIPHER_ALG_SERPENT_256:
    case QCRYPTO_CIPHER_ALG_TWOFISH_128:
    case QCRYPTO_CIPHER_ALG_TWOFISH_192:
    case QCRYPTO_CIPHER_ALG_TWOFISH_256:
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
    case QCRYPTO_CIPHER_MODE_XTS:
        break;
    default:
        error_setg(errp, "Unsupported cipher mode %d", mode);
        return NULL;
    }

    if (!qcrypto_cipher_validate_key_length(alg, mode, nkey, errp)) {
        return NULL;
    }

    cipher = g_new0(QCryptoCipher, 1);
    cipher->alg = alg;
    cipher->mode = mode;

    ctx = g_new0(QCryptoCipherNettle, 1);

    switch (alg) {
    case QCRYPTO_CIPHER_ALG_DES_RFB:
        ctx->ctx = g_new0(struct des_ctx, 1);
        rfbkey = qcrypto_cipher_munge_des_rfb_key(key, nkey);
        des_set_key(ctx->ctx, rfbkey);
        g_free(rfbkey);

        ctx->alg_encrypt = des_encrypt_wrapper;
        ctx->alg_decrypt = des_decrypt_wrapper;

        ctx->blocksize = DES_BLOCK_SIZE;
        break;

    case QCRYPTO_CIPHER_ALG_AES_128:
    case QCRYPTO_CIPHER_ALG_AES_192:
    case QCRYPTO_CIPHER_ALG_AES_256:
        ctx->ctx = g_new0(QCryptoNettleAES, 1);

        if (mode == QCRYPTO_CIPHER_MODE_XTS) {
            ctx->ctx_tweak = g_new0(QCryptoNettleAES, 1);

            nkey /= 2;
            aes_set_encrypt_key(&((QCryptoNettleAES *)ctx->ctx)->enc,
                                nkey, key);
            aes_set_decrypt_key(&((QCryptoNettleAES *)ctx->ctx)->dec,
                                nkey, key);

            aes_set_encrypt_key(&((QCryptoNettleAES *)ctx->ctx_tweak)->enc,
                                nkey, key + nkey);
            aes_set_decrypt_key(&((QCryptoNettleAES *)ctx->ctx_tweak)->dec,
                                nkey, key + nkey);
        } else {
            aes_set_encrypt_key(&((QCryptoNettleAES *)ctx->ctx)->enc,
                                nkey, key);
            aes_set_decrypt_key(&((QCryptoNettleAES *)ctx->ctx)->dec,
                                nkey, key);
        }

        ctx->alg_encrypt = aes_encrypt_wrapper;
        ctx->alg_decrypt = aes_decrypt_wrapper;

        ctx->blocksize = AES_BLOCK_SIZE;
        break;

    case QCRYPTO_CIPHER_ALG_CAST5_128:
        ctx->ctx = g_new0(struct cast128_ctx, 1);

        if (mode == QCRYPTO_CIPHER_MODE_XTS) {
            ctx->ctx_tweak = g_new0(struct cast128_ctx, 1);

            nkey /= 2;
            cast5_set_key(ctx->ctx, nkey, key);
            cast5_set_key(ctx->ctx_tweak, nkey, key + nkey);
        } else {
            cast5_set_key(ctx->ctx, nkey, key);
        }

        ctx->alg_encrypt = cast128_encrypt_wrapper;
        ctx->alg_decrypt = cast128_decrypt_wrapper;

        ctx->blocksize = CAST128_BLOCK_SIZE;
        break;

    case QCRYPTO_CIPHER_ALG_SERPENT_128:
    case QCRYPTO_CIPHER_ALG_SERPENT_192:
    case QCRYPTO_CIPHER_ALG_SERPENT_256:
        ctx->ctx = g_new0(struct serpent_ctx, 1);

        if (mode == QCRYPTO_CIPHER_MODE_XTS) {
            ctx->ctx_tweak = g_new0(struct serpent_ctx, 1);

            nkey /= 2;
            serpent_set_key(ctx->ctx, nkey, key);
            serpent_set_key(ctx->ctx_tweak, nkey, key + nkey);
        } else {
            serpent_set_key(ctx->ctx, nkey, key);
        }

        ctx->alg_encrypt = serpent_encrypt_wrapper;
        ctx->alg_decrypt = serpent_decrypt_wrapper;

        ctx->blocksize = SERPENT_BLOCK_SIZE;
        break;

    case QCRYPTO_CIPHER_ALG_TWOFISH_128:
    case QCRYPTO_CIPHER_ALG_TWOFISH_192:
    case QCRYPTO_CIPHER_ALG_TWOFISH_256:
        ctx->ctx = g_new0(struct twofish_ctx, 1);

        if (mode == QCRYPTO_CIPHER_MODE_XTS) {
            ctx->ctx_tweak = g_new0(struct twofish_ctx, 1);

            nkey /= 2;
            twofish_set_key(ctx->ctx, nkey, key);
            twofish_set_key(ctx->ctx_tweak, nkey, key + nkey);
        } else {
            twofish_set_key(ctx->ctx, nkey, key);
        }

        ctx->alg_encrypt = twofish_encrypt_wrapper;
        ctx->alg_decrypt = twofish_decrypt_wrapper;

        ctx->blocksize = TWOFISH_BLOCK_SIZE;
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
    g_free(ctx->ctx);
    g_free(ctx->ctx_tweak);
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
        ctx->alg_encrypt(ctx->ctx, len, out, in);
        break;

    case QCRYPTO_CIPHER_MODE_CBC:
        cbc_encrypt(ctx->ctx, ctx->alg_encrypt,
                    ctx->blocksize, ctx->iv,
                    len, out, in);
        break;

    case QCRYPTO_CIPHER_MODE_XTS:
        xts_encrypt(ctx->ctx, ctx->ctx_tweak,
                    ctx->alg_encrypt, ctx->alg_encrypt,
                    ctx->iv, len, out, in);
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
        ctx->alg_decrypt(ctx->ctx, len, out, in);
        break;

    case QCRYPTO_CIPHER_MODE_CBC:
        cbc_decrypt(ctx->ctx, ctx->alg_decrypt,
                    ctx->blocksize, ctx->iv,
                    len, out, in);
        break;

    case QCRYPTO_CIPHER_MODE_XTS:
        if (ctx->blocksize != XTS_BLOCK_SIZE) {
            error_setg(errp, "Block size must be %d not %zu",
                       XTS_BLOCK_SIZE, ctx->blocksize);
            return -1;
        }
        xts_decrypt(ctx->ctx, ctx->ctx_tweak,
                    ctx->alg_encrypt, ctx->alg_decrypt,
                    ctx->iv, len, out, in);
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
