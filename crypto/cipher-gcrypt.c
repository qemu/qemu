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
#include <gcrypt.h>


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

typedef struct QCryptoCipherGcrypt QCryptoCipherGcrypt;
struct QCryptoCipherGcrypt {
    gcry_cipher_hd_t handle;
    size_t blocksize;
};

QCryptoCipher *qcrypto_cipher_new(QCryptoCipherAlgorithm alg,
                                  QCryptoCipherMode mode,
                                  const uint8_t *key, size_t nkey,
                                  Error **errp)
{
    QCryptoCipher *cipher;
    QCryptoCipherGcrypt *ctx;
    gcry_error_t err;
    int gcryalg, gcrymode;

    switch (mode) {
    case QCRYPTO_CIPHER_MODE_ECB:
        gcrymode = GCRY_CIPHER_MODE_ECB;
        break;
    case QCRYPTO_CIPHER_MODE_CBC:
        gcrymode = GCRY_CIPHER_MODE_CBC;
        break;
    default:
        error_setg(errp, "Unsupported cipher mode %d", mode);
        return NULL;
    }

    if (!qcrypto_cipher_validate_key_length(alg, nkey, errp)) {
        return NULL;
    }

    switch (alg) {
    case QCRYPTO_CIPHER_ALG_DES_RFB:
        gcryalg = GCRY_CIPHER_DES;
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

    default:
        error_setg(errp, "Unsupported cipher algorithm %d", alg);
        return NULL;
    }

    cipher = g_new0(QCryptoCipher, 1);
    cipher->alg = alg;
    cipher->mode = mode;

    ctx = g_new0(QCryptoCipherGcrypt, 1);

    err = gcry_cipher_open(&ctx->handle, gcryalg, gcrymode, 0);
    if (err != 0) {
        error_setg(errp, "Cannot initialize cipher: %s",
                   gcry_strerror(err));
        goto error;
    }

    if (cipher->alg == QCRYPTO_CIPHER_ALG_DES_RFB) {
        /* We're using standard DES cipher from gcrypt, so we need
         * to munge the key so that the results are the same as the
         * bizarre RFB variant of DES :-)
         */
        uint8_t *rfbkey = qcrypto_cipher_munge_des_rfb_key(key, nkey);
        err = gcry_cipher_setkey(ctx->handle, rfbkey, nkey);
        g_free(rfbkey);
        ctx->blocksize = 8;
    } else {
        err = gcry_cipher_setkey(ctx->handle, key, nkey);
        ctx->blocksize = 16;
    }
    if (err != 0) {
        error_setg(errp, "Cannot set key: %s",
                   gcry_strerror(err));
        goto error;
    }

    cipher->opaque = ctx;
    return cipher;

 error:
    gcry_cipher_close(ctx->handle);
    g_free(ctx);
    g_free(cipher);
    return NULL;
}


void qcrypto_cipher_free(QCryptoCipher *cipher)
{
    QCryptoCipherGcrypt *ctx;
    if (!cipher) {
        return;
    }
    ctx = cipher->opaque;
    gcry_cipher_close(ctx->handle);
    g_free(ctx);
    g_free(cipher);
}


int qcrypto_cipher_encrypt(QCryptoCipher *cipher,
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

    err = gcry_cipher_encrypt(ctx->handle,
                              out, len,
                              in, len);
    if (err != 0) {
        error_setg(errp, "Cannot encrypt data: %s",
                   gcry_strerror(err));
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
    QCryptoCipherGcrypt *ctx = cipher->opaque;
    gcry_error_t err;

    if (len % ctx->blocksize) {
        error_setg(errp, "Length %zu must be a multiple of block size %zu",
                   len, ctx->blocksize);
        return -1;
    }

    err = gcry_cipher_decrypt(ctx->handle,
                              out, len,
                              in, len);
    if (err != 0) {
        error_setg(errp, "Cannot decrypt data: %s",
                   gcry_strerror(err));
        return -1;
    }

    return 0;
}

int qcrypto_cipher_setiv(QCryptoCipher *cipher,
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

    gcry_cipher_reset(ctx->handle);
    err = gcry_cipher_setiv(ctx->handle, iv, niv);
    if (err != 0) {
        error_setg(errp, "Cannot set IV: %s",
                   gcry_strerror(err));
        return -1;
    }

    return 0;
}
