/*
 * QEMU Crypto cipher libgcrypt algorithms
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
#include "cipherpriv.h"

#include <gcrypt.h>


bool qcrypto_cipher_supports(QCryptoCipherAlgorithm alg,
                             QCryptoCipherMode mode)
{
    switch (alg) {
    case QCRYPTO_CIPHER_ALG_DES_RFB:
    case QCRYPTO_CIPHER_ALG_3DES:
    case QCRYPTO_CIPHER_ALG_AES_128:
    case QCRYPTO_CIPHER_ALG_AES_192:
    case QCRYPTO_CIPHER_ALG_AES_256:
    case QCRYPTO_CIPHER_ALG_CAST5_128:
    case QCRYPTO_CIPHER_ALG_SERPENT_128:
    case QCRYPTO_CIPHER_ALG_SERPENT_192:
    case QCRYPTO_CIPHER_ALG_SERPENT_256:
    case QCRYPTO_CIPHER_ALG_TWOFISH_128:
    case QCRYPTO_CIPHER_ALG_TWOFISH_256:
        break;
    default:
        return false;
    }

    switch (mode) {
    case QCRYPTO_CIPHER_MODE_ECB:
    case QCRYPTO_CIPHER_MODE_CBC:
    case QCRYPTO_CIPHER_MODE_XTS:
    case QCRYPTO_CIPHER_MODE_CTR:
        return true;
    default:
        return false;
    }
}

typedef struct QCryptoCipherGcrypt QCryptoCipherGcrypt;
struct QCryptoCipherGcrypt {
    gcry_cipher_hd_t handle;
    gcry_cipher_hd_t tweakhandle;
    size_t blocksize;
    /* Initialization vector or Counter */
    uint8_t *iv;
};

static void
qcrypto_gcrypt_cipher_free_ctx(QCryptoCipherGcrypt *ctx,
                               QCryptoCipherMode mode)
{
    if (!ctx) {
        return;
    }

    gcry_cipher_close(ctx->handle);
    if (mode == QCRYPTO_CIPHER_MODE_XTS) {
        gcry_cipher_close(ctx->tweakhandle);
    }
    g_free(ctx->iv);
    g_free(ctx);
}


static QCryptoCipherGcrypt *qcrypto_cipher_ctx_new(QCryptoCipherAlgorithm alg,
                                                   QCryptoCipherMode mode,
                                                   const uint8_t *key,
                                                   size_t nkey,
                                                   Error **errp)
{
    QCryptoCipherGcrypt *ctx;
    gcry_error_t err;
    int gcryalg, gcrymode;

    switch (mode) {
    case QCRYPTO_CIPHER_MODE_ECB:
    case QCRYPTO_CIPHER_MODE_XTS:
        gcrymode = GCRY_CIPHER_MODE_ECB;
        break;
    case QCRYPTO_CIPHER_MODE_CBC:
        gcrymode = GCRY_CIPHER_MODE_CBC;
        break;
    case QCRYPTO_CIPHER_MODE_CTR:
        gcrymode = GCRY_CIPHER_MODE_CTR;
        break;
    default:
        error_setg(errp, "Unsupported cipher mode %s",
                   QCryptoCipherMode_lookup[mode]);
        return NULL;
    }

    if (!qcrypto_cipher_validate_key_length(alg, mode, nkey, errp)) {
        return NULL;
    }

    switch (alg) {
    case QCRYPTO_CIPHER_ALG_DES_RFB:
        gcryalg = GCRY_CIPHER_DES;
        break;

    case QCRYPTO_CIPHER_ALG_3DES:
        gcryalg = GCRY_CIPHER_3DES;
        break;

    case QCRYPTO_CIPHER_ALG_AES_128:
        gcryalg = GCRY_CIPHER_AES128;
        break;

    case QCRYPTO_CIPHER_ALG_AES_192:
        gcryalg = GCRY_CIPHER_AES192;
        break;

    case QCRYPTO_CIPHER_ALG_AES_256:
        gcryalg = GCRY_CIPHER_AES256;
        break;

    case QCRYPTO_CIPHER_ALG_CAST5_128:
        gcryalg = GCRY_CIPHER_CAST5;
        break;

    case QCRYPTO_CIPHER_ALG_SERPENT_128:
        gcryalg = GCRY_CIPHER_SERPENT128;
        break;

    case QCRYPTO_CIPHER_ALG_SERPENT_192:
        gcryalg = GCRY_CIPHER_SERPENT192;
        break;

    case QCRYPTO_CIPHER_ALG_SERPENT_256:
        gcryalg = GCRY_CIPHER_SERPENT256;
        break;

    case QCRYPTO_CIPHER_ALG_TWOFISH_128:
        gcryalg = GCRY_CIPHER_TWOFISH128;
        break;

    case QCRYPTO_CIPHER_ALG_TWOFISH_256:
        gcryalg = GCRY_CIPHER_TWOFISH;
        break;

    default:
        error_setg(errp, "Unsupported cipher algorithm %s",
                   QCryptoCipherAlgorithm_lookup[alg]);
        return NULL;
    }

    ctx = g_new0(QCryptoCipherGcrypt, 1);

    err = gcry_cipher_open(&ctx->handle, gcryalg, gcrymode, 0);
    if (err != 0) {
        error_setg(errp, "Cannot initialize cipher: %s",
                   gcry_strerror(err));
        goto error;
    }
    if (mode == QCRYPTO_CIPHER_MODE_XTS) {
        err = gcry_cipher_open(&ctx->tweakhandle, gcryalg, gcrymode, 0);
        if (err != 0) {
            error_setg(errp, "Cannot initialize cipher: %s",
                       gcry_strerror(err));
            goto error;
        }
    }

    if (alg == QCRYPTO_CIPHER_ALG_DES_RFB) {
        /* We're using standard DES cipher from gcrypt, so we need
         * to munge the key so that the results are the same as the
         * bizarre RFB variant of DES :-)
         */
        uint8_t *rfbkey = qcrypto_cipher_munge_des_rfb_key(key, nkey);
        err = gcry_cipher_setkey(ctx->handle, rfbkey, nkey);
        g_free(rfbkey);
        ctx->blocksize = 8;
    } else {
        if (mode == QCRYPTO_CIPHER_MODE_XTS) {
            nkey /= 2;
            err = gcry_cipher_setkey(ctx->handle, key, nkey);
            if (err != 0) {
                error_setg(errp, "Cannot set key: %s",
                           gcry_strerror(err));
                goto error;
            }
            err = gcry_cipher_setkey(ctx->tweakhandle, key + nkey, nkey);
        } else {
            err = gcry_cipher_setkey(ctx->handle, key, nkey);
        }
        if (err != 0) {
            error_setg(errp, "Cannot set key: %s",
                       gcry_strerror(err));
            goto error;
        }
        switch (alg) {
        case QCRYPTO_CIPHER_ALG_AES_128:
        case QCRYPTO_CIPHER_ALG_AES_192:
        case QCRYPTO_CIPHER_ALG_AES_256:
        case QCRYPTO_CIPHER_ALG_SERPENT_128:
        case QCRYPTO_CIPHER_ALG_SERPENT_192:
        case QCRYPTO_CIPHER_ALG_SERPENT_256:
        case QCRYPTO_CIPHER_ALG_TWOFISH_128:
        case QCRYPTO_CIPHER_ALG_TWOFISH_256:
            ctx->blocksize = 16;
            break;
        case QCRYPTO_CIPHER_ALG_3DES:
        case QCRYPTO_CIPHER_ALG_CAST5_128:
            ctx->blocksize = 8;
            break;
        default:
            g_assert_not_reached();
        }
    }

    if (mode == QCRYPTO_CIPHER_MODE_XTS) {
        if (ctx->blocksize != XTS_BLOCK_SIZE) {
            error_setg(errp,
                       "Cipher block size %zu must equal XTS block size %d",
                       ctx->blocksize, XTS_BLOCK_SIZE);
            goto error;
        }
        ctx->iv = g_new0(uint8_t, ctx->blocksize);
    }

    return ctx;

 error:
    qcrypto_gcrypt_cipher_free_ctx(ctx, mode);
    return NULL;
}


static void
qcrypto_gcrypt_cipher_ctx_free(QCryptoCipher *cipher)
{
    qcrypto_gcrypt_cipher_free_ctx(cipher->opaque, cipher->mode);
}


static void qcrypto_gcrypt_xts_encrypt(const void *ctx,
                                       size_t length,
                                       uint8_t *dst,
                                       const uint8_t *src)
{
    gcry_error_t err;
    err = gcry_cipher_encrypt((gcry_cipher_hd_t)ctx, dst, length, src, length);
    g_assert(err == 0);
}

static void qcrypto_gcrypt_xts_decrypt(const void *ctx,
                                       size_t length,
                                       uint8_t *dst,
                                       const uint8_t *src)
{
    gcry_error_t err;
    err = gcry_cipher_decrypt((gcry_cipher_hd_t)ctx, dst, length, src, length);
    g_assert(err == 0);
}

static int
qcrypto_gcrypt_cipher_encrypt(QCryptoCipher *cipher,
                              const void *in,
                              void *out,
                              size_t len,
                              Error **errp)
{
    QCryptoCipherGcrypt *ctx = cipher->opaque;
    gcry_error_t err;

    if (len % ctx->blocksize) {
        error_setg(errp, "Length %zu must be a multiple of block size %zu",
                   len, ctx->blocksize);
        return -1;
    }

    if (cipher->mode == QCRYPTO_CIPHER_MODE_XTS) {
        xts_encrypt(ctx->handle, ctx->tweakhandle,
                    qcrypto_gcrypt_xts_encrypt,
                    qcrypto_gcrypt_xts_decrypt,
                    ctx->iv, len, out, in);
    } else {
        err = gcry_cipher_encrypt(ctx->handle,
                                  out, len,
                                  in, len);
        if (err != 0) {
            error_setg(errp, "Cannot encrypt data: %s",
                       gcry_strerror(err));
            return -1;
        }
    }

    return 0;
}


static int
qcrypto_gcrypt_cipher_decrypt(QCryptoCipher *cipher,
                              const void *in,
                              void *out,
                              size_t len,
                              Error **errp)
{
    QCryptoCipherGcrypt *ctx = cipher->opaque;
    gcry_error_t err;

    if (len % ctx->blocksize) {
        error_setg(errp, "Length %zu must be a multiple of block size %zu",
                   len, ctx->blocksize);
        return -1;
    }

    if (cipher->mode == QCRYPTO_CIPHER_MODE_XTS) {
        xts_decrypt(ctx->handle, ctx->tweakhandle,
                    qcrypto_gcrypt_xts_encrypt,
                    qcrypto_gcrypt_xts_decrypt,
                    ctx->iv, len, out, in);
    } else {
        err = gcry_cipher_decrypt(ctx->handle,
                                  out, len,
                                  in, len);
        if (err != 0) {
            error_setg(errp, "Cannot decrypt data: %s",
                       gcry_strerror(err));
            return -1;
        }
    }

    return 0;
}

static int
qcrypto_gcrypt_cipher_setiv(QCryptoCipher *cipher,
                            const uint8_t *iv, size_t niv,
                            Error **errp)
{
    QCryptoCipherGcrypt *ctx = cipher->opaque;
    gcry_error_t err;

    if (niv != ctx->blocksize) {
        error_setg(errp, "Expected IV size %zu not %zu",
                   ctx->blocksize, niv);
        return -1;
    }

    if (ctx->iv) {
        memcpy(ctx->iv, iv, niv);
    } else {
        if (cipher->mode == QCRYPTO_CIPHER_MODE_CTR) {
            err = gcry_cipher_setctr(ctx->handle, iv, niv);
            if (err != 0) {
                error_setg(errp, "Cannot set Counter: %s",
                       gcry_strerror(err));
                return -1;
            }
        } else {
            gcry_cipher_reset(ctx->handle);
            err = gcry_cipher_setiv(ctx->handle, iv, niv);
            if (err != 0) {
                error_setg(errp, "Cannot set IV: %s",
                       gcry_strerror(err));
                return -1;
            }
        }
    }

    return 0;
}


static struct QCryptoCipherDriver qcrypto_cipher_lib_driver = {
    .cipher_encrypt = qcrypto_gcrypt_cipher_encrypt,
    .cipher_decrypt = qcrypto_gcrypt_cipher_decrypt,
    .cipher_setiv = qcrypto_gcrypt_cipher_setiv,
    .cipher_free = qcrypto_gcrypt_cipher_ctx_free,
};
