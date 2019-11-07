/*
 * QEMU Crypto cipher nettle algorithms
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

#include "qemu/osdep.h"
#ifdef CONFIG_QEMU_PRIVATE_XTS
#include "crypto/xts.h"
#endif
#include "cipherpriv.h"

#include <nettle/nettle-types.h>
#include <nettle/aes.h>
#include <nettle/des.h>
#include <nettle/cbc.h>
#include <nettle/cast128.h>
#include <nettle/serpent.h>
#include <nettle/twofish.h>
#include <nettle/ctr.h>
#ifndef CONFIG_QEMU_PRIVATE_XTS
#include <nettle/xts.h>
#endif

typedef void (*QCryptoCipherNettleFuncWrapper)(const void *ctx,
                                               size_t length,
                                               uint8_t *dst,
                                               const uint8_t *src);

#if CONFIG_NETTLE_VERSION_MAJOR < 3
typedef nettle_crypt_func * QCryptoCipherNettleFuncNative;
typedef void *       cipher_ctx_t;
typedef unsigned     cipher_length_t;

#define cast5_set_key cast128_set_key

#define aes128_ctx aes_ctx
#define aes192_ctx aes_ctx
#define aes256_ctx aes_ctx
#define aes128_set_encrypt_key(c, k) \
    aes_set_encrypt_key(c, 16, k)
#define aes192_set_encrypt_key(c, k) \
    aes_set_encrypt_key(c, 24, k)
#define aes256_set_encrypt_key(c, k) \
    aes_set_encrypt_key(c, 32, k)
#define aes128_set_decrypt_key(c, k) \
    aes_set_decrypt_key(c, 16, k)
#define aes192_set_decrypt_key(c, k) \
    aes_set_decrypt_key(c, 24, k)
#define aes256_set_decrypt_key(c, k) \
    aes_set_decrypt_key(c, 32, k)
#define aes128_encrypt aes_encrypt
#define aes192_encrypt aes_encrypt
#define aes256_encrypt aes_encrypt
#define aes128_decrypt aes_decrypt
#define aes192_decrypt aes_decrypt
#define aes256_decrypt aes_decrypt
#else
typedef nettle_cipher_func * QCryptoCipherNettleFuncNative;
typedef const void * cipher_ctx_t;
typedef size_t       cipher_length_t;
#endif

typedef struct QCryptoNettleAES128 {
    struct aes128_ctx enc;
    struct aes128_ctx dec;
} QCryptoNettleAES128;

typedef struct QCryptoNettleAES192 {
    struct aes192_ctx enc;
    struct aes192_ctx dec;
} QCryptoNettleAES192;

typedef struct QCryptoNettleAES256 {
    struct aes256_ctx enc;
    struct aes256_ctx dec;
} QCryptoNettleAES256;

static void aes128_encrypt_native(cipher_ctx_t ctx, cipher_length_t length,
                                  uint8_t *dst, const uint8_t *src)
{
    const QCryptoNettleAES128 *aesctx = ctx;
    aes128_encrypt(&aesctx->enc, length, dst, src);
}

static void aes128_decrypt_native(cipher_ctx_t ctx, cipher_length_t length,
                                  uint8_t *dst, const uint8_t *src)
{
    const QCryptoNettleAES128 *aesctx = ctx;
    aes128_decrypt(&aesctx->dec, length, dst, src);
}

static void aes192_encrypt_native(cipher_ctx_t ctx, cipher_length_t length,
                               uint8_t *dst, const uint8_t *src)
{
    const QCryptoNettleAES192 *aesctx = ctx;
    aes192_encrypt(&aesctx->enc, length, dst, src);
}

static void aes192_decrypt_native(cipher_ctx_t ctx, cipher_length_t length,
                               uint8_t *dst, const uint8_t *src)
{
    const QCryptoNettleAES192 *aesctx = ctx;
    aes192_decrypt(&aesctx->dec, length, dst, src);
}

static void aes256_encrypt_native(cipher_ctx_t ctx, cipher_length_t length,
                               uint8_t *dst, const uint8_t *src)
{
    const QCryptoNettleAES256 *aesctx = ctx;
    aes256_encrypt(&aesctx->enc, length, dst, src);
}

static void aes256_decrypt_native(cipher_ctx_t ctx, cipher_length_t length,
                               uint8_t *dst, const uint8_t *src)
{
    const QCryptoNettleAES256 *aesctx = ctx;
    aes256_decrypt(&aesctx->dec, length, dst, src);
}

static void des_encrypt_native(cipher_ctx_t ctx, cipher_length_t length,
                               uint8_t *dst, const uint8_t *src)
{
    des_encrypt(ctx, length, dst, src);
}

static void des_decrypt_native(cipher_ctx_t ctx, cipher_length_t length,
                               uint8_t *dst, const uint8_t *src)
{
    des_decrypt(ctx, length, dst, src);
}

static void des3_encrypt_native(cipher_ctx_t ctx, cipher_length_t length,
                                uint8_t *dst, const uint8_t *src)
{
    des3_encrypt(ctx, length, dst, src);
}

static void des3_decrypt_native(cipher_ctx_t ctx, cipher_length_t length,
                                uint8_t *dst, const uint8_t *src)
{
    des3_decrypt(ctx, length, dst, src);
}

static void cast128_encrypt_native(cipher_ctx_t ctx, cipher_length_t length,
                                   uint8_t *dst, const uint8_t *src)
{
    cast128_encrypt(ctx, length, dst, src);
}

static void cast128_decrypt_native(cipher_ctx_t ctx, cipher_length_t length,
                                   uint8_t *dst, const uint8_t *src)
{
    cast128_decrypt(ctx, length, dst, src);
}

static void serpent_encrypt_native(cipher_ctx_t ctx, cipher_length_t length,
                                   uint8_t *dst, const uint8_t *src)
{
    serpent_encrypt(ctx, length, dst, src);
}

static void serpent_decrypt_native(cipher_ctx_t ctx, cipher_length_t length,
                                   uint8_t *dst, const uint8_t *src)
{
    serpent_decrypt(ctx, length, dst, src);
}

static void twofish_encrypt_native(cipher_ctx_t ctx, cipher_length_t length,
                                   uint8_t *dst, const uint8_t *src)
{
    twofish_encrypt(ctx, length, dst, src);
}

static void twofish_decrypt_native(cipher_ctx_t ctx, cipher_length_t length,
                                   uint8_t *dst, const uint8_t *src)
{
    twofish_decrypt(ctx, length, dst, src);
}

static void aes128_encrypt_wrapper(const void *ctx, size_t length,
                                uint8_t *dst, const uint8_t *src)
{
    const QCryptoNettleAES128 *aesctx = ctx;
    aes128_encrypt(&aesctx->enc, length, dst, src);
}

static void aes128_decrypt_wrapper(const void *ctx, size_t length,
                                uint8_t *dst, const uint8_t *src)
{
    const QCryptoNettleAES128 *aesctx = ctx;
    aes128_decrypt(&aesctx->dec, length, dst, src);
}

static void aes192_encrypt_wrapper(const void *ctx, size_t length,
                                uint8_t *dst, const uint8_t *src)
{
    const QCryptoNettleAES192 *aesctx = ctx;
    aes192_encrypt(&aesctx->enc, length, dst, src);
}

static void aes192_decrypt_wrapper(const void *ctx, size_t length,
                                uint8_t *dst, const uint8_t *src)
{
    const QCryptoNettleAES192 *aesctx = ctx;
    aes192_decrypt(&aesctx->dec, length, dst, src);
}

static void aes256_encrypt_wrapper(const void *ctx, size_t length,
                                uint8_t *dst, const uint8_t *src)
{
    const QCryptoNettleAES256 *aesctx = ctx;
    aes256_encrypt(&aesctx->enc, length, dst, src);
}

static void aes256_decrypt_wrapper(const void *ctx, size_t length,
                                uint8_t *dst, const uint8_t *src)
{
    const QCryptoNettleAES256 *aesctx = ctx;
    aes256_decrypt(&aesctx->dec, length, dst, src);
}

static void des_encrypt_wrapper(const void *ctx, size_t length,
                                uint8_t *dst, const uint8_t *src)
{
    des_encrypt(ctx, length, dst, src);
}

static void des_decrypt_wrapper(const void *ctx, size_t length,
                                uint8_t *dst, const uint8_t *src)
{
    des_decrypt(ctx, length, dst, src);
}

static void des3_encrypt_wrapper(const void *ctx, size_t length,
                                uint8_t *dst, const uint8_t *src)
{
    des3_encrypt(ctx, length, dst, src);
}

static void des3_decrypt_wrapper(const void *ctx, size_t length,
                                uint8_t *dst, const uint8_t *src)
{
    des3_decrypt(ctx, length, dst, src);
}

static void cast128_encrypt_wrapper(const void *ctx, size_t length,
                                    uint8_t *dst, const uint8_t *src)
{
    cast128_encrypt(ctx, length, dst, src);
}

static void cast128_decrypt_wrapper(const void *ctx, size_t length,
                                    uint8_t *dst, const uint8_t *src)
{
    cast128_decrypt(ctx, length, dst, src);
}

static void serpent_encrypt_wrapper(const void *ctx, size_t length,
                                    uint8_t *dst, const uint8_t *src)
{
    serpent_encrypt(ctx, length, dst, src);
}

static void serpent_decrypt_wrapper(const void *ctx, size_t length,
                                    uint8_t *dst, const uint8_t *src)
{
    serpent_decrypt(ctx, length, dst, src);
}

static void twofish_encrypt_wrapper(const void *ctx, size_t length,
                                    uint8_t *dst, const uint8_t *src)
{
    twofish_encrypt(ctx, length, dst, src);
}

static void twofish_decrypt_wrapper(const void *ctx, size_t length,
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
    QCryptoCipherNettleFuncNative alg_encrypt_native;
    QCryptoCipherNettleFuncNative alg_decrypt_native;
    QCryptoCipherNettleFuncWrapper alg_encrypt_wrapper;
    QCryptoCipherNettleFuncWrapper alg_decrypt_wrapper;
    /* Initialization vector or Counter */
    uint8_t *iv;
    size_t blocksize;
};

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
    case QCRYPTO_CIPHER_ALG_TWOFISH_192:
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


static void
qcrypto_nettle_cipher_free_ctx(QCryptoCipherNettle *ctx)
{
    if (!ctx) {
        return;
    }

    g_free(ctx->iv);
    g_free(ctx->ctx);
    g_free(ctx->ctx_tweak);
    g_free(ctx);
}


static QCryptoCipherNettle *qcrypto_cipher_ctx_new(QCryptoCipherAlgorithm alg,
                                                   QCryptoCipherMode mode,
                                                   const uint8_t *key,
                                                   size_t nkey,
                                                   Error **errp)
{
    QCryptoCipherNettle *ctx;
    uint8_t *rfbkey;

    switch (mode) {
    case QCRYPTO_CIPHER_MODE_ECB:
    case QCRYPTO_CIPHER_MODE_CBC:
    case QCRYPTO_CIPHER_MODE_XTS:
    case QCRYPTO_CIPHER_MODE_CTR:
        break;
    default:
        error_setg(errp, "Unsupported cipher mode %s",
                   QCryptoCipherMode_str(mode));
        return NULL;
    }

    if (!qcrypto_cipher_validate_key_length(alg, mode, nkey, errp)) {
        return NULL;
    }

    ctx = g_new0(QCryptoCipherNettle, 1);

    switch (alg) {
    case QCRYPTO_CIPHER_ALG_DES_RFB:
        ctx->ctx = g_new0(struct des_ctx, 1);
        rfbkey = qcrypto_cipher_munge_des_rfb_key(key, nkey);
        des_set_key(ctx->ctx, rfbkey);
        g_free(rfbkey);

        ctx->alg_encrypt_native = des_encrypt_native;
        ctx->alg_decrypt_native = des_decrypt_native;
        ctx->alg_encrypt_wrapper = des_encrypt_wrapper;
        ctx->alg_decrypt_wrapper = des_decrypt_wrapper;

        ctx->blocksize = DES_BLOCK_SIZE;
        break;

    case QCRYPTO_CIPHER_ALG_3DES:
        ctx->ctx = g_new0(struct des3_ctx, 1);
        des3_set_key(ctx->ctx, key);

        ctx->alg_encrypt_native = des3_encrypt_native;
        ctx->alg_decrypt_native = des3_decrypt_native;
        ctx->alg_encrypt_wrapper = des3_encrypt_wrapper;
        ctx->alg_decrypt_wrapper = des3_decrypt_wrapper;

        ctx->blocksize = DES3_BLOCK_SIZE;
        break;

    case QCRYPTO_CIPHER_ALG_AES_128:
        ctx->ctx = g_new0(QCryptoNettleAES128, 1);

        if (mode == QCRYPTO_CIPHER_MODE_XTS) {
            ctx->ctx_tweak = g_new0(QCryptoNettleAES128, 1);

            nkey /= 2;
            aes128_set_encrypt_key(&((QCryptoNettleAES128 *)ctx->ctx)->enc,
                                   key);
            aes128_set_decrypt_key(&((QCryptoNettleAES128 *)ctx->ctx)->dec,
                                   key);

            aes128_set_encrypt_key(&((QCryptoNettleAES128 *)ctx->ctx_tweak)->
                                   enc, key + nkey);
            aes128_set_decrypt_key(&((QCryptoNettleAES128 *)ctx->ctx_tweak)->
                                   dec, key + nkey);
        } else {
            aes128_set_encrypt_key(&((QCryptoNettleAES128 *)ctx->ctx)->enc,
                                   key);
            aes128_set_decrypt_key(&((QCryptoNettleAES128 *)ctx->ctx)->dec,
                                   key);
        }

        ctx->alg_encrypt_native = aes128_encrypt_native;
        ctx->alg_decrypt_native = aes128_decrypt_native;
        ctx->alg_encrypt_wrapper = aes128_encrypt_wrapper;
        ctx->alg_decrypt_wrapper = aes128_decrypt_wrapper;

        ctx->blocksize = AES_BLOCK_SIZE;
        break;

    case QCRYPTO_CIPHER_ALG_AES_192:
        ctx->ctx = g_new0(QCryptoNettleAES192, 1);

        if (mode == QCRYPTO_CIPHER_MODE_XTS) {
            ctx->ctx_tweak = g_new0(QCryptoNettleAES192, 1);

            nkey /= 2;
            aes192_set_encrypt_key(&((QCryptoNettleAES192 *)ctx->ctx)->enc,
                                   key);
            aes192_set_decrypt_key(&((QCryptoNettleAES192 *)ctx->ctx)->dec,
                                   key);

            aes192_set_encrypt_key(&((QCryptoNettleAES192 *)ctx->ctx_tweak)->
                                   enc, key + nkey);
            aes192_set_decrypt_key(&((QCryptoNettleAES192 *)ctx->ctx_tweak)->
                                   dec, key + nkey);
        } else {
            aes192_set_encrypt_key(&((QCryptoNettleAES192 *)ctx->ctx)->enc,
                                   key);
            aes192_set_decrypt_key(&((QCryptoNettleAES192 *)ctx->ctx)->dec,
                                   key);
        }

        ctx->alg_encrypt_native = aes192_encrypt_native;
        ctx->alg_decrypt_native = aes192_decrypt_native;
        ctx->alg_encrypt_wrapper = aes192_encrypt_wrapper;
        ctx->alg_decrypt_wrapper = aes192_decrypt_wrapper;

        ctx->blocksize = AES_BLOCK_SIZE;
        break;

    case QCRYPTO_CIPHER_ALG_AES_256:
        ctx->ctx = g_new0(QCryptoNettleAES256, 1);

        if (mode == QCRYPTO_CIPHER_MODE_XTS) {
            ctx->ctx_tweak = g_new0(QCryptoNettleAES256, 1);

            nkey /= 2;
            aes256_set_encrypt_key(&((QCryptoNettleAES256 *)ctx->ctx)->enc,
                                   key);
            aes256_set_decrypt_key(&((QCryptoNettleAES256 *)ctx->ctx)->dec,
                                   key);

            aes256_set_encrypt_key(&((QCryptoNettleAES256 *)ctx->ctx_tweak)->
                                   enc, key + nkey);
            aes256_set_decrypt_key(&((QCryptoNettleAES256 *)ctx->ctx_tweak)->
                                   dec, key + nkey);
        } else {
            aes256_set_encrypt_key(&((QCryptoNettleAES256 *)ctx->ctx)->enc,
                                   key);
            aes256_set_decrypt_key(&((QCryptoNettleAES256 *)ctx->ctx)->dec,
                                   key);
        }

        ctx->alg_encrypt_native = aes256_encrypt_native;
        ctx->alg_decrypt_native = aes256_decrypt_native;
        ctx->alg_encrypt_wrapper = aes256_encrypt_wrapper;
        ctx->alg_decrypt_wrapper = aes256_decrypt_wrapper;

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

        ctx->alg_encrypt_native = cast128_encrypt_native;
        ctx->alg_decrypt_native = cast128_decrypt_native;
        ctx->alg_encrypt_wrapper = cast128_encrypt_wrapper;
        ctx->alg_decrypt_wrapper = cast128_decrypt_wrapper;

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

        ctx->alg_encrypt_native = serpent_encrypt_native;
        ctx->alg_decrypt_native = serpent_decrypt_native;
        ctx->alg_encrypt_wrapper = serpent_encrypt_wrapper;
        ctx->alg_decrypt_wrapper = serpent_decrypt_wrapper;

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

        ctx->alg_encrypt_native = twofish_encrypt_native;
        ctx->alg_decrypt_native = twofish_decrypt_native;
        ctx->alg_encrypt_wrapper = twofish_encrypt_wrapper;
        ctx->alg_decrypt_wrapper = twofish_decrypt_wrapper;

        ctx->blocksize = TWOFISH_BLOCK_SIZE;
        break;

    default:
        error_setg(errp, "Unsupported cipher algorithm %s",
                   QCryptoCipherAlgorithm_str(alg));
        goto error;
    }

    if (mode == QCRYPTO_CIPHER_MODE_XTS &&
        ctx->blocksize != XTS_BLOCK_SIZE) {
        error_setg(errp, "Cipher block size %zu must equal XTS block size %d",
                   ctx->blocksize, XTS_BLOCK_SIZE);
        goto error;
    }

    ctx->iv = g_new0(uint8_t, ctx->blocksize);

    return ctx;

 error:
    qcrypto_nettle_cipher_free_ctx(ctx);
    return NULL;
}


static void
qcrypto_nettle_cipher_ctx_free(QCryptoCipher *cipher)
{
    QCryptoCipherNettle *ctx;

    ctx = cipher->opaque;
    qcrypto_nettle_cipher_free_ctx(ctx);
}


static int
qcrypto_nettle_cipher_encrypt(QCryptoCipher *cipher,
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
        ctx->alg_encrypt_wrapper(ctx->ctx, len, out, in);
        break;

    case QCRYPTO_CIPHER_MODE_CBC:
        cbc_encrypt(ctx->ctx, ctx->alg_encrypt_native,
                    ctx->blocksize, ctx->iv,
                    len, out, in);
        break;

    case QCRYPTO_CIPHER_MODE_XTS:
#ifdef CONFIG_QEMU_PRIVATE_XTS
        xts_encrypt(ctx->ctx, ctx->ctx_tweak,
                    ctx->alg_encrypt_wrapper, ctx->alg_encrypt_wrapper,
                    ctx->iv, len, out, in);
#else
        xts_encrypt_message(ctx->ctx, ctx->ctx_tweak,
                            ctx->alg_encrypt_native,
                            ctx->iv, len, out, in);
#endif
        break;

    case QCRYPTO_CIPHER_MODE_CTR:
        ctr_crypt(ctx->ctx, ctx->alg_encrypt_native,
                    ctx->blocksize, ctx->iv,
                    len, out, in);
        break;

    default:
        error_setg(errp, "Unsupported cipher mode %s",
                   QCryptoCipherMode_str(cipher->mode));
        return -1;
    }
    return 0;
}


static int
qcrypto_nettle_cipher_decrypt(QCryptoCipher *cipher,
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
        ctx->alg_decrypt_wrapper(ctx->ctx, len, out, in);
        break;

    case QCRYPTO_CIPHER_MODE_CBC:
        cbc_decrypt(ctx->ctx, ctx->alg_decrypt_native,
                    ctx->blocksize, ctx->iv,
                    len, out, in);
        break;

    case QCRYPTO_CIPHER_MODE_XTS:
#ifdef CONFIG_QEMU_PRIVATE_XTS
        xts_decrypt(ctx->ctx, ctx->ctx_tweak,
                    ctx->alg_encrypt_wrapper, ctx->alg_decrypt_wrapper,
                    ctx->iv, len, out, in);
#else
        xts_decrypt_message(ctx->ctx, ctx->ctx_tweak,
                            ctx->alg_decrypt_native,
                            ctx->alg_encrypt_native,
                            ctx->iv, len, out, in);
#endif
        break;
    case QCRYPTO_CIPHER_MODE_CTR:
        ctr_crypt(ctx->ctx, ctx->alg_encrypt_native,
                    ctx->blocksize, ctx->iv,
                    len, out, in);
        break;

    default:
        error_setg(errp, "Unsupported cipher mode %s",
                   QCryptoCipherMode_str(cipher->mode));
        return -1;
    }
    return 0;
}

static int
qcrypto_nettle_cipher_setiv(QCryptoCipher *cipher,
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


static struct QCryptoCipherDriver qcrypto_cipher_lib_driver = {
    .cipher_encrypt = qcrypto_nettle_cipher_encrypt,
    .cipher_decrypt = qcrypto_nettle_cipher_decrypt,
    .cipher_setiv = qcrypto_nettle_cipher_setiv,
    .cipher_free = qcrypto_nettle_cipher_ctx_free,
};
