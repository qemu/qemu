/*
 * QEMU Crypto cipher gnutls algorithms
 *
 * Copyright (c) 2021 Red Hat, Inc.
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

#include "qemu/osdep.h"
#include "cipherpriv.h"

#include <gnutls/crypto.h>

#if GNUTLS_VERSION_NUMBER >= 0x030608
#define QEMU_GNUTLS_XTS
#endif

bool qcrypto_cipher_supports(QCryptoCipherAlgorithm alg,
                             QCryptoCipherMode mode)
{

    switch (mode) {
    case QCRYPTO_CIPHER_MODE_ECB:
    case QCRYPTO_CIPHER_MODE_CBC:
        switch (alg) {
        case QCRYPTO_CIPHER_ALG_AES_128:
        case QCRYPTO_CIPHER_ALG_AES_192:
        case QCRYPTO_CIPHER_ALG_AES_256:
        case QCRYPTO_CIPHER_ALG_DES:
        case QCRYPTO_CIPHER_ALG_3DES:
            return true;
        default:
            return false;
        }
#ifdef QEMU_GNUTLS_XTS
    case QCRYPTO_CIPHER_MODE_XTS:
        switch (alg) {
        case QCRYPTO_CIPHER_ALG_AES_128:
        case QCRYPTO_CIPHER_ALG_AES_256:
            return true;
        default:
            return false;
        }
#endif
    default:
        return false;
    }
}

typedef struct QCryptoCipherGnutls QCryptoCipherGnutls;
struct QCryptoCipherGnutls {
    QCryptoCipher base;
    gnutls_cipher_hd_t handle; /* XTS & CBC mode */
    gnutls_cipher_algorithm_t galg; /* ECB mode */
    guint8 *key; /* ECB mode */
    size_t nkey; /* ECB mode */
    size_t blocksize;
};


static void
qcrypto_gnutls_cipher_free(QCryptoCipher *cipher)
{
    QCryptoCipherGnutls *ctx = container_of(cipher, QCryptoCipherGnutls, base);

    g_free(ctx->key);
    if (ctx->handle) {
        gnutls_cipher_deinit(ctx->handle);
    }
    g_free(ctx);
}


static int
qcrypto_gnutls_cipher_encrypt(QCryptoCipher *cipher,
                              const void *in,
                              void *out,
                              size_t len,
                              Error **errp)
{
    QCryptoCipherGnutls *ctx = container_of(cipher, QCryptoCipherGnutls, base);
    int err;

    if (len % ctx->blocksize) {
        error_setg(errp, "Length %zu must be a multiple of block size %zu",
                   len, ctx->blocksize);
        return -1;
    }

    if (ctx->handle) { /* CBC / XTS mode */
        err = gnutls_cipher_encrypt2(ctx->handle,
                                     in, len,
                                     out, len);
        if (err != 0) {
            error_setg(errp, "Cannot encrypt data: %s",
                       gnutls_strerror(err));
            return -1;
        }
    } else { /* ECB mode very inefficiently faked with CBC */
        g_autofree unsigned char *iv = g_new0(unsigned char, ctx->blocksize);
        while (len) {
            gnutls_cipher_hd_t handle;
            gnutls_datum_t gkey = { (unsigned char *)ctx->key, ctx->nkey };
            err = gnutls_cipher_init(&handle, ctx->galg, &gkey, NULL);
            if (err != 0) {
                error_setg(errp, "Cannot initialize cipher: %s",
                           gnutls_strerror(err));
                return -1;
            }

            gnutls_cipher_set_iv(handle, iv, ctx->blocksize);

            err = gnutls_cipher_encrypt2(handle,
                                         in, ctx->blocksize,
                                         out, ctx->blocksize);
            if (err != 0) {
                gnutls_cipher_deinit(handle);
                error_setg(errp, "Cannot encrypt data: %s",
                           gnutls_strerror(err));
                return -1;
            }
            gnutls_cipher_deinit(handle);

            len -= ctx->blocksize;
            in += ctx->blocksize;
            out += ctx->blocksize;
        }
    }

    return 0;
}


static int
qcrypto_gnutls_cipher_decrypt(QCryptoCipher *cipher,
                              const void *in,
                              void *out,
                              size_t len,
                              Error **errp)
{
    QCryptoCipherGnutls *ctx = container_of(cipher, QCryptoCipherGnutls, base);
    int err;

    if (len % ctx->blocksize) {
        error_setg(errp, "Length %zu must be a multiple of block size %zu",
                   len, ctx->blocksize);
        return -1;
    }

    if (ctx->handle) { /* CBC / XTS mode */
        err = gnutls_cipher_decrypt2(ctx->handle,
                                     in, len,
                                     out, len);

        if (err != 0) {
            error_setg(errp, "Cannot decrypt data: %s",
                       gnutls_strerror(err));
            return -1;
        }
    } else { /* ECB mode very inefficiently faked with CBC */
        g_autofree unsigned char *iv = g_new0(unsigned char, ctx->blocksize);
        while (len) {
            gnutls_cipher_hd_t handle;
            gnutls_datum_t gkey = { (unsigned char *)ctx->key, ctx->nkey };
            err = gnutls_cipher_init(&handle, ctx->galg, &gkey, NULL);
            if (err != 0) {
                error_setg(errp, "Cannot initialize cipher: %s",
                           gnutls_strerror(err));
                return -1;
            }

            gnutls_cipher_set_iv(handle, iv, ctx->blocksize);

            err = gnutls_cipher_decrypt2(handle,
                                         in, ctx->blocksize,
                                         out, ctx->blocksize);
            if (err != 0) {
                gnutls_cipher_deinit(handle);
                error_setg(errp, "Cannot encrypt data: %s",
                           gnutls_strerror(err));
                return -1;
            }
            gnutls_cipher_deinit(handle);

            len -= ctx->blocksize;
            in += ctx->blocksize;
            out += ctx->blocksize;
        }
    }

    return 0;
}

static int
qcrypto_gnutls_cipher_setiv(QCryptoCipher *cipher,
                            const uint8_t *iv, size_t niv,
                            Error **errp)
{
    QCryptoCipherGnutls *ctx = container_of(cipher, QCryptoCipherGnutls, base);

    if (niv != ctx->blocksize) {
        error_setg(errp, "Expected IV size %zu not %zu",
                   ctx->blocksize, niv);
        return -1;
    }

    gnutls_cipher_set_iv(ctx->handle, (unsigned char *)iv, niv);

    return 0;
}


static struct QCryptoCipherDriver gnutls_driver = {
    .cipher_encrypt = qcrypto_gnutls_cipher_encrypt,
    .cipher_decrypt = qcrypto_gnutls_cipher_decrypt,
    .cipher_setiv = qcrypto_gnutls_cipher_setiv,
    .cipher_free = qcrypto_gnutls_cipher_free,
};

static QCryptoCipher *qcrypto_cipher_ctx_new(QCryptoCipherAlgorithm alg,
                                             QCryptoCipherMode mode,
                                             const uint8_t *key,
                                             size_t nkey,
                                             Error **errp)
{
    QCryptoCipherGnutls *ctx;
    gnutls_datum_t gkey = { (unsigned char *)key, nkey };
    gnutls_cipher_algorithm_t galg = GNUTLS_CIPHER_UNKNOWN;
    int err;

    switch (mode) {
#ifdef QEMU_GNUTLS_XTS
    case QCRYPTO_CIPHER_MODE_XTS:
        switch (alg) {
        case QCRYPTO_CIPHER_ALG_AES_128:
            galg = GNUTLS_CIPHER_AES_128_XTS;
            break;
        case QCRYPTO_CIPHER_ALG_AES_256:
            galg = GNUTLS_CIPHER_AES_256_XTS;
            break;
        default:
            break;
        }
        break;
#endif

    case QCRYPTO_CIPHER_MODE_ECB:
    case QCRYPTO_CIPHER_MODE_CBC:
        switch (alg) {
        case QCRYPTO_CIPHER_ALG_AES_128:
            galg = GNUTLS_CIPHER_AES_128_CBC;
            break;
        case QCRYPTO_CIPHER_ALG_AES_192:
            galg = GNUTLS_CIPHER_AES_192_CBC;
            break;
        case QCRYPTO_CIPHER_ALG_AES_256:
            galg = GNUTLS_CIPHER_AES_256_CBC;
            break;
        case QCRYPTO_CIPHER_ALG_DES:
            galg = GNUTLS_CIPHER_DES_CBC;
            break;
        case QCRYPTO_CIPHER_ALG_3DES:
            galg = GNUTLS_CIPHER_3DES_CBC;
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }

    if (galg == GNUTLS_CIPHER_UNKNOWN) {
        error_setg(errp, "Unsupported cipher algorithm %s with %s mode",
                   QCryptoCipherAlgorithm_str(alg),
                   QCryptoCipherMode_str(mode));
        return NULL;
    }

    if (!qcrypto_cipher_validate_key_length(alg, mode, nkey, errp)) {
        return NULL;
    }

    ctx = g_new0(QCryptoCipherGnutls, 1);
    ctx->base.driver = &gnutls_driver;

    if (mode == QCRYPTO_CIPHER_MODE_ECB) {
        ctx->key = g_new0(guint8, nkey);
        memcpy(ctx->key, key, nkey);
        ctx->nkey = nkey;
        ctx->galg = galg;
    } else {
        err = gnutls_cipher_init(&ctx->handle, galg, &gkey, NULL);
        if (err != 0) {
            error_setg(errp, "Cannot initialize cipher: %s",
                       gnutls_strerror(err));
            goto error;
        }
    }

    if (alg == QCRYPTO_CIPHER_ALG_DES ||
        alg == QCRYPTO_CIPHER_ALG_3DES)
        ctx->blocksize = 8;
    else
        ctx->blocksize = 16;

    /*
     * Our API contract for requires iv to be optional
     * but nettle gets unhappy when called by gnutls
     * in this case, so we just force set a default
     * all-zeros IV, to match behaviour of other backends.
     */
    if (mode != QCRYPTO_CIPHER_MODE_ECB) {
        g_autofree unsigned char *iv = g_new0(unsigned char, ctx->blocksize);
        gnutls_cipher_set_iv(ctx->handle, iv, ctx->blocksize);
    }

    return &ctx->base;

 error:
    qcrypto_gnutls_cipher_free(&ctx->base);
    return NULL;
}
