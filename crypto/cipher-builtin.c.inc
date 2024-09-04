/*
 * QEMU Crypto cipher built-in algorithms
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#include "crypto/aes.h"

typedef struct QCryptoCipherBuiltinAESContext QCryptoCipherBuiltinAESContext;
struct QCryptoCipherBuiltinAESContext {
    AES_KEY enc;
    AES_KEY dec;
};

typedef struct QCryptoCipherBuiltinAES QCryptoCipherBuiltinAES;
struct QCryptoCipherBuiltinAES {
    QCryptoCipher base;
    QCryptoCipherBuiltinAESContext key;
    uint8_t iv[AES_BLOCK_SIZE];
};


static inline bool qcrypto_length_check(size_t len, size_t blocksize,
                                        Error **errp)
{
    if (unlikely(len & (blocksize - 1))) {
        error_setg(errp, "Length %zu must be a multiple of block size %zu",
                   len, blocksize);
        return false;
    }
    return true;
}

static void qcrypto_cipher_ctx_free(QCryptoCipher *cipher)
{
    g_free(cipher);
}

static int qcrypto_cipher_no_setiv(QCryptoCipher *cipher,
                                   const uint8_t *iv, size_t niv,
                                   Error **errp)
{
    error_setg(errp, "Setting IV is not supported");
    return -1;
}

static void do_aes_encrypt_ecb(const void *vctx,
                               size_t len,
                               uint8_t *out,
                               const uint8_t *in)
{
    const QCryptoCipherBuiltinAESContext *ctx = vctx;

    /* We have already verified that len % AES_BLOCK_SIZE == 0. */
    while (len) {
        AES_encrypt(in, out, &ctx->enc);
        in += AES_BLOCK_SIZE;
        out += AES_BLOCK_SIZE;
        len -= AES_BLOCK_SIZE;
    }
}

static void do_aes_decrypt_ecb(const void *vctx,
                               size_t len,
                               uint8_t *out,
                               const uint8_t *in)
{
    const QCryptoCipherBuiltinAESContext *ctx = vctx;

    /* We have already verified that len % AES_BLOCK_SIZE == 0. */
    while (len) {
        AES_decrypt(in, out, &ctx->dec);
        in += AES_BLOCK_SIZE;
        out += AES_BLOCK_SIZE;
        len -= AES_BLOCK_SIZE;
    }
}

static void do_aes_encrypt_cbc(const AES_KEY *key,
                               size_t len,
                               uint8_t *out,
                               const uint8_t *in,
                               uint8_t *ivec)
{
    uint8_t tmp[AES_BLOCK_SIZE];
    size_t n;

    /* We have already verified that len % AES_BLOCK_SIZE == 0. */
    while (len) {
        for (n = 0; n < AES_BLOCK_SIZE; ++n) {
            tmp[n] = in[n] ^ ivec[n];
        }
        AES_encrypt(tmp, out, key);
        memcpy(ivec, out, AES_BLOCK_SIZE);
        len -= AES_BLOCK_SIZE;
        in += AES_BLOCK_SIZE;
        out += AES_BLOCK_SIZE;
    }
}

static void do_aes_decrypt_cbc(const AES_KEY *key,
                               size_t len,
                               uint8_t *out,
                               const uint8_t *in,
                               uint8_t *ivec)
{
    uint8_t tmp[AES_BLOCK_SIZE];
    size_t n;

    /* We have already verified that len % AES_BLOCK_SIZE == 0. */
    while (len) {
        memcpy(tmp, in, AES_BLOCK_SIZE);
        AES_decrypt(in, out, key);
        for (n = 0; n < AES_BLOCK_SIZE; ++n) {
            out[n] ^= ivec[n];
        }
        memcpy(ivec, tmp, AES_BLOCK_SIZE);
        len -= AES_BLOCK_SIZE;
        in += AES_BLOCK_SIZE;
        out += AES_BLOCK_SIZE;
    }
}

static int qcrypto_cipher_aes_encrypt_ecb(QCryptoCipher *cipher,
                                          const void *in, void *out,
                                          size_t len, Error **errp)
{
    QCryptoCipherBuiltinAES *ctx
        = container_of(cipher, QCryptoCipherBuiltinAES, base);

    if (!qcrypto_length_check(len, AES_BLOCK_SIZE, errp)) {
        return -1;
    }
    do_aes_encrypt_ecb(&ctx->key, len, out, in);
    return 0;
}

static int qcrypto_cipher_aes_decrypt_ecb(QCryptoCipher *cipher,
                                          const void *in, void *out,
                                          size_t len, Error **errp)
{
    QCryptoCipherBuiltinAES *ctx
        = container_of(cipher, QCryptoCipherBuiltinAES, base);

    if (!qcrypto_length_check(len, AES_BLOCK_SIZE, errp)) {
        return -1;
    }
    do_aes_decrypt_ecb(&ctx->key, len, out, in);
    return 0;
}

static int qcrypto_cipher_aes_encrypt_cbc(QCryptoCipher *cipher,
                                          const void *in, void *out,
                                          size_t len, Error **errp)
{
    QCryptoCipherBuiltinAES *ctx
        = container_of(cipher, QCryptoCipherBuiltinAES, base);

    if (!qcrypto_length_check(len, AES_BLOCK_SIZE, errp)) {
        return -1;
    }
    do_aes_encrypt_cbc(&ctx->key.enc, len, out, in, ctx->iv);
    return 0;
}

static int qcrypto_cipher_aes_decrypt_cbc(QCryptoCipher *cipher,
                                          const void *in, void *out,
                                          size_t len, Error **errp)
{
    QCryptoCipherBuiltinAES *ctx
        = container_of(cipher, QCryptoCipherBuiltinAES, base);

    if (!qcrypto_length_check(len, AES_BLOCK_SIZE, errp)) {
        return -1;
    }
    do_aes_decrypt_cbc(&ctx->key.dec, len, out, in, ctx->iv);
    return 0;
}

static int qcrypto_cipher_aes_setiv(QCryptoCipher *cipher, const uint8_t *iv,
                             size_t niv, Error **errp)
{
    QCryptoCipherBuiltinAES *ctx
        = container_of(cipher, QCryptoCipherBuiltinAES, base);

    if (niv != AES_BLOCK_SIZE) {
        error_setg(errp, "IV must be %d bytes not %zu",
                   AES_BLOCK_SIZE, niv);
        return -1;
    }

    memcpy(ctx->iv, iv, AES_BLOCK_SIZE);
    return 0;
}

static const struct QCryptoCipherDriver qcrypto_cipher_aes_driver_ecb = {
    .cipher_encrypt = qcrypto_cipher_aes_encrypt_ecb,
    .cipher_decrypt = qcrypto_cipher_aes_decrypt_ecb,
    .cipher_setiv = qcrypto_cipher_no_setiv,
    .cipher_free = qcrypto_cipher_ctx_free,
};

static const struct QCryptoCipherDriver qcrypto_cipher_aes_driver_cbc = {
    .cipher_encrypt = qcrypto_cipher_aes_encrypt_cbc,
    .cipher_decrypt = qcrypto_cipher_aes_decrypt_cbc,
    .cipher_setiv = qcrypto_cipher_aes_setiv,
    .cipher_free = qcrypto_cipher_ctx_free,
};

bool qcrypto_cipher_supports(QCryptoCipherAlgo alg,
                             QCryptoCipherMode mode)
{
    switch (alg) {
    case QCRYPTO_CIPHER_ALGO_AES_128:
    case QCRYPTO_CIPHER_ALGO_AES_192:
    case QCRYPTO_CIPHER_ALGO_AES_256:
        switch (mode) {
        case QCRYPTO_CIPHER_MODE_ECB:
        case QCRYPTO_CIPHER_MODE_CBC:
            return true;
        default:
            return false;
        }
        break;
    default:
        return false;
    }
}

static QCryptoCipher *qcrypto_cipher_ctx_new(QCryptoCipherAlgo alg,
                                             QCryptoCipherMode mode,
                                             const uint8_t *key,
                                             size_t nkey,
                                             Error **errp)
{
    if (!qcrypto_cipher_validate_key_length(alg, mode, nkey, errp)) {
        return NULL;
    }

    switch (alg) {
    case QCRYPTO_CIPHER_ALGO_AES_128:
    case QCRYPTO_CIPHER_ALGO_AES_192:
    case QCRYPTO_CIPHER_ALGO_AES_256:
        {
            QCryptoCipherBuiltinAES *ctx;
            const QCryptoCipherDriver *drv;

            switch (mode) {
            case QCRYPTO_CIPHER_MODE_ECB:
                drv = &qcrypto_cipher_aes_driver_ecb;
                break;
            case QCRYPTO_CIPHER_MODE_CBC:
                drv = &qcrypto_cipher_aes_driver_cbc;
                break;
            default:
                goto bad_mode;
            }

            ctx = g_new0(QCryptoCipherBuiltinAES, 1);
            ctx->base.driver = drv;

            if (AES_set_encrypt_key(key, nkey * 8, &ctx->key.enc)) {
                error_setg(errp, "Failed to set encryption key");
                goto error;
            }
            if (AES_set_decrypt_key(key, nkey * 8, &ctx->key.dec)) {
                error_setg(errp, "Failed to set decryption key");
                goto error;
            }

            return &ctx->base;

        error:
            g_free(ctx);
            return NULL;
        }

    default:
        error_setg(errp,
                   "Unsupported cipher algorithm %s",
                   QCryptoCipherAlgo_str(alg));
        return NULL;
    }

 bad_mode:
    error_setg(errp, "Unsupported cipher mode %s",
               QCryptoCipherMode_str(mode));
    return NULL;
}
