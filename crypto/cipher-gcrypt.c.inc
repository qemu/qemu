/*
 * QEMU Crypto cipher libgcrypt algorithms
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

#include <gcrypt.h>

static int qcrypto_cipher_alg_to_gcry_alg(QCryptoCipherAlgo alg)
{
    switch (alg) {
    case QCRYPTO_CIPHER_ALGO_DES:
        return GCRY_CIPHER_DES;
    case QCRYPTO_CIPHER_ALGO_3DES:
        return GCRY_CIPHER_3DES;
    case QCRYPTO_CIPHER_ALGO_AES_128:
        return GCRY_CIPHER_AES128;
    case QCRYPTO_CIPHER_ALGO_AES_192:
        return GCRY_CIPHER_AES192;
    case QCRYPTO_CIPHER_ALGO_AES_256:
        return GCRY_CIPHER_AES256;
    case QCRYPTO_CIPHER_ALGO_CAST5_128:
        return GCRY_CIPHER_CAST5;
    case QCRYPTO_CIPHER_ALGO_SERPENT_128:
        return GCRY_CIPHER_SERPENT128;
    case QCRYPTO_CIPHER_ALGO_SERPENT_192:
        return GCRY_CIPHER_SERPENT192;
    case QCRYPTO_CIPHER_ALGO_SERPENT_256:
        return GCRY_CIPHER_SERPENT256;
    case QCRYPTO_CIPHER_ALGO_TWOFISH_128:
        return GCRY_CIPHER_TWOFISH128;
    case QCRYPTO_CIPHER_ALGO_TWOFISH_256:
        return GCRY_CIPHER_TWOFISH;
#ifdef CONFIG_CRYPTO_SM4
    case QCRYPTO_CIPHER_ALGO_SM4:
        return GCRY_CIPHER_SM4;
#endif
    default:
        return GCRY_CIPHER_NONE;
    }
}

static int qcrypto_cipher_mode_to_gcry_mode(QCryptoCipherMode mode)
{
    switch (mode) {
    case QCRYPTO_CIPHER_MODE_ECB:
        return GCRY_CIPHER_MODE_ECB;
    case QCRYPTO_CIPHER_MODE_XTS:
        return GCRY_CIPHER_MODE_XTS;
    case QCRYPTO_CIPHER_MODE_CBC:
        return GCRY_CIPHER_MODE_CBC;
    case QCRYPTO_CIPHER_MODE_CTR:
        return GCRY_CIPHER_MODE_CTR;
    case QCRYPTO_CIPHER_MODE_GCM:
        return GCRY_CIPHER_MODE_GCM;
    default:
        return GCRY_CIPHER_MODE_NONE;
    }
}

bool qcrypto_cipher_supports(QCryptoCipherAlgo alg,
                             QCryptoCipherMode mode)
{
    switch (alg) {
    case QCRYPTO_CIPHER_ALGO_DES:
    case QCRYPTO_CIPHER_ALGO_3DES:
    case QCRYPTO_CIPHER_ALGO_AES_128:
    case QCRYPTO_CIPHER_ALGO_AES_192:
    case QCRYPTO_CIPHER_ALGO_AES_256:
    case QCRYPTO_CIPHER_ALGO_CAST5_128:
    case QCRYPTO_CIPHER_ALGO_SERPENT_128:
    case QCRYPTO_CIPHER_ALGO_SERPENT_192:
    case QCRYPTO_CIPHER_ALGO_SERPENT_256:
    case QCRYPTO_CIPHER_ALGO_TWOFISH_128:
    case QCRYPTO_CIPHER_ALGO_TWOFISH_256:
#ifdef CONFIG_CRYPTO_SM4
    case QCRYPTO_CIPHER_ALGO_SM4:
#endif
        break;
    default:
        return false;
    }

    if (gcry_cipher_algo_info(qcrypto_cipher_alg_to_gcry_alg(alg),
                              GCRYCTL_TEST_ALGO, NULL, NULL) != 0) {
        return false;
    }

    switch (mode) {
    case QCRYPTO_CIPHER_MODE_ECB:
    case QCRYPTO_CIPHER_MODE_CBC:
    case QCRYPTO_CIPHER_MODE_XTS:
    case QCRYPTO_CIPHER_MODE_CTR:
        return true;
    case QCRYPTO_CIPHER_MODE_GCM:
        /* GCM requires a 128-bit block cipher. */
        return gcry_cipher_get_algo_blklen(
                   qcrypto_cipher_alg_to_gcry_alg(alg)) == 16;
    default:
        return false;
    }
}

typedef struct QCryptoCipherGcrypt {
    QCryptoCipher base;
    gcry_cipher_hd_t handle;
    size_t blocksize;
} QCryptoCipherGcrypt;


static void qcrypto_gcrypt_ctx_free(QCryptoCipher *cipher)
{
    QCryptoCipherGcrypt *ctx = container_of(cipher, QCryptoCipherGcrypt, base);

    gcry_cipher_close(ctx->handle);
    g_free(ctx);
}

static int qcrypto_gcrypt_encrypt(QCryptoCipher *cipher, const void *in,
                                  void *out, size_t len, Error **errp)
{
    QCryptoCipherGcrypt *ctx = container_of(cipher, QCryptoCipherGcrypt, base);
    gcry_error_t err;

    if (len & (ctx->blocksize - 1)) {
        error_setg(errp, "Length %zu must be a multiple of block size %zu",
                   len, ctx->blocksize);
        return -1;
    }

    err = gcry_cipher_encrypt(ctx->handle, out, len, in, len);
    if (err != 0) {
        error_setg(errp, "Cannot encrypt data: %s", gcry_strerror(err));
        return -1;
    }

    return 0;
}


static int qcrypto_gcrypt_decrypt(QCryptoCipher *cipher, const void *in,
                                  void *out, size_t len, Error **errp)
{
    QCryptoCipherGcrypt *ctx = container_of(cipher, QCryptoCipherGcrypt, base);
    gcry_error_t err;

    if (len & (ctx->blocksize - 1)) {
        error_setg(errp, "Length %zu must be a multiple of block size %zu",
                   len, ctx->blocksize);
        return -1;
    }

    err = gcry_cipher_decrypt(ctx->handle, out, len, in, len);
    if (err != 0) {
        error_setg(errp, "Cannot decrypt data: %s",
                   gcry_strerror(err));
        return -1;
    }

    return 0;
}

static int qcrypto_gcrypt_setiv(QCryptoCipher *cipher,
                                const uint8_t *iv, size_t niv,
                                Error **errp)
{
    QCryptoCipherGcrypt *ctx = container_of(cipher, QCryptoCipherGcrypt, base);
    gcry_error_t err;

    if (niv != ctx->blocksize) {
        error_setg(errp, "Expected IV size %zu not %zu",
                   ctx->blocksize, niv);
        return -1;
    }

    gcry_cipher_reset(ctx->handle);
    err = gcry_cipher_setiv(ctx->handle, iv, niv);
    if (err != 0) {
        error_setg(errp, "Cannot set IV: %s", gcry_strerror(err));
        return -1;
    }

    return 0;
}

static int qcrypto_gcrypt_ctr_setiv(QCryptoCipher *cipher,
                                    const uint8_t *iv, size_t niv,
                                    Error **errp)
{
    QCryptoCipherGcrypt *ctx = container_of(cipher, QCryptoCipherGcrypt, base);
    gcry_error_t err;

    if (niv != ctx->blocksize) {
        error_setg(errp, "Expected IV size %zu not %zu",
                   ctx->blocksize, niv);
        return -1;
    }

    err = gcry_cipher_setctr(ctx->handle, iv, niv);
    if (err != 0) {
        error_setg(errp, "Cannot set Counter: %s", gcry_strerror(err));
        return -1;
    }

    return 0;
}


static const struct QCryptoCipherDriver qcrypto_gcrypt_driver = {
    .cipher_encrypt = qcrypto_gcrypt_encrypt,
    .cipher_decrypt = qcrypto_gcrypt_decrypt,
    .cipher_setiv = qcrypto_gcrypt_setiv,
    .cipher_free = qcrypto_gcrypt_ctx_free,
};

static const struct QCryptoCipherDriver qcrypto_gcrypt_ctr_driver = {
    .cipher_encrypt = qcrypto_gcrypt_encrypt,
    .cipher_decrypt = qcrypto_gcrypt_decrypt,
    .cipher_setiv = qcrypto_gcrypt_ctr_setiv,
    .cipher_free = qcrypto_gcrypt_ctx_free,
};

/*
 * GCM is an AEAD stream mode: the IV/nonce need not match the block size,
 * the message length need not be a multiple of the block size, associated
 * data is fed with gcry_cipher_authenticate() and the authentication tag is
 * read back with gcry_cipher_gettag().
 */
static int qcrypto_gcrypt_gcm_setiv(QCryptoCipher *cipher,
                                    const uint8_t *iv, size_t niv,
                                    Error **errp)
{
    QCryptoCipherGcrypt *ctx = container_of(cipher, QCryptoCipherGcrypt, base);
    gcry_error_t err;

    gcry_cipher_reset(ctx->handle);
    err = gcry_cipher_setiv(ctx->handle, iv, niv);
    if (err != 0) {
        error_setg(errp, "Cannot set IV: %s", gcry_strerror(err));
        return -1;
    }

    return 0;
}

static int qcrypto_gcrypt_gcm_setaad(QCryptoCipher *cipher,
                                     const uint8_t *aad, size_t len,
                                     Error **errp)
{
    QCryptoCipherGcrypt *ctx = container_of(cipher, QCryptoCipherGcrypt, base);
    gcry_error_t err;

    err = gcry_cipher_authenticate(ctx->handle, aad, len);
    if (err != 0) {
        error_setg(errp, "Cannot set AAD: %s", gcry_strerror(err));
        return -1;
    }

    return 0;
}

static int qcrypto_gcrypt_gcm_encrypt(QCryptoCipher *cipher, const void *in,
                                      void *out, size_t len, Error **errp)
{
    QCryptoCipherGcrypt *ctx = container_of(cipher, QCryptoCipherGcrypt, base);
    gcry_error_t err;

    err = gcry_cipher_encrypt(ctx->handle, out, len, in, len);
    if (err != 0) {
        error_setg(errp, "Cannot encrypt data: %s", gcry_strerror(err));
        return -1;
    }

    return 0;
}

static int qcrypto_gcrypt_gcm_decrypt(QCryptoCipher *cipher, const void *in,
                                      void *out, size_t len, Error **errp)
{
    QCryptoCipherGcrypt *ctx = container_of(cipher, QCryptoCipherGcrypt, base);
    gcry_error_t err;

    err = gcry_cipher_decrypt(ctx->handle, out, len, in, len);
    if (err != 0) {
        error_setg(errp, "Cannot decrypt data: %s", gcry_strerror(err));
        return -1;
    }

    return 0;
}

static int qcrypto_gcrypt_gcm_gettag(QCryptoCipher *cipher,
                                     uint8_t *tag, size_t len, Error **errp)
{
    QCryptoCipherGcrypt *ctx = container_of(cipher, QCryptoCipherGcrypt, base);
    gcry_error_t err;

    err = gcry_cipher_gettag(ctx->handle, tag, len);
    if (err != 0) {
        error_setg(errp, "Cannot get tag: %s", gcry_strerror(err));
        return -1;
    }

    return 0;
}

static const struct QCryptoCipherDriver qcrypto_gcrypt_gcm_driver = {
    .cipher_encrypt = qcrypto_gcrypt_gcm_encrypt,
    .cipher_decrypt = qcrypto_gcrypt_gcm_decrypt,
    .cipher_setiv = qcrypto_gcrypt_gcm_setiv,
    .cipher_setaad = qcrypto_gcrypt_gcm_setaad,
    .cipher_gettag = qcrypto_gcrypt_gcm_gettag,
    .cipher_free = qcrypto_gcrypt_ctx_free,
};

static QCryptoCipher *qcrypto_cipher_ctx_new(QCryptoCipherAlgo alg,
                                             QCryptoCipherMode mode,
                                             const uint8_t *key,
                                             size_t nkey,
                                             Error **errp)
{
    QCryptoCipherGcrypt *ctx;
    const QCryptoCipherDriver *drv;
    gcry_error_t err;
    int gcryalg, gcrymode;

    if (!qcrypto_cipher_validate_key_length(alg, mode, nkey, errp)) {
        return NULL;
    }

    gcryalg = qcrypto_cipher_alg_to_gcry_alg(alg);
    if (gcryalg == GCRY_CIPHER_NONE) {
        error_setg(errp, "Unsupported cipher algorithm %s",
                   QCryptoCipherAlgo_str(alg));
        return NULL;
    }

    gcrymode = qcrypto_cipher_mode_to_gcry_mode(mode);
    if (gcrymode == GCRY_CIPHER_MODE_NONE) {
        error_setg(errp, "Unsupported cipher mode %s",
                   QCryptoCipherMode_str(mode));
        return NULL;
    }

    if (mode == QCRYPTO_CIPHER_MODE_CTR) {
        drv = &qcrypto_gcrypt_ctr_driver;
    } else if (mode == QCRYPTO_CIPHER_MODE_GCM) {
        drv = &qcrypto_gcrypt_gcm_driver;
    } else {
        drv = &qcrypto_gcrypt_driver;
    }

    ctx = g_new0(QCryptoCipherGcrypt, 1);
    ctx->base.driver = drv;

    err = gcry_cipher_open(&ctx->handle, gcryalg, gcrymode, 0);
    if (err != 0) {
        error_setg(errp, "Cannot initialize cipher: %s",
                   gcry_strerror(err));
        goto error;
    }
    ctx->blocksize = gcry_cipher_get_algo_blklen(gcryalg);

    err = gcry_cipher_setkey(ctx->handle, key, nkey);
    if (err != 0) {
        error_setg(errp, "Cannot set key: %s", gcry_strerror(err));
        goto error;
    }

    return &ctx->base;

 error:
    gcry_cipher_close(ctx->handle);
    g_free(ctx);
    return NULL;
}
