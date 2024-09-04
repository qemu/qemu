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

#ifdef CONFIG_QEMU_PRIVATE_XTS
#include "crypto/xts.h"
#endif

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
#ifdef CONFIG_CRYPTO_SM4
#include <nettle/sm4.h>
#endif

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


static void qcrypto_cipher_ctx_free(QCryptoCipher *ctx)
{
    g_free(ctx);
}

static int qcrypto_cipher_no_setiv(QCryptoCipher *cipher,
                                   const uint8_t *iv, size_t niv,
                                   Error **errp)
{
    error_setg(errp, "Setting IV is not supported");
    return -1;
}


#define DEFINE_SETIV(NAME, TYPE, BLEN)                                  \
static int NAME##_setiv(QCryptoCipher *cipher, const uint8_t *iv,       \
                        size_t niv, Error **errp)                       \
{                                                                       \
    TYPE *ctx = container_of(cipher, TYPE, base);                       \
    if (niv != BLEN) {                                                  \
        error_setg(errp, "Expected IV size %d not %zu", BLEN, niv);     \
        return -1;                                                      \
    }                                                                   \
    memcpy(ctx->iv, iv, niv);                                           \
    return 0;                                                           \
}


#define DEFINE_ECB(NAME, TYPE, BLEN, ENCRYPT, DECRYPT)                  \
static int NAME##_encrypt_ecb(QCryptoCipher *cipher, const void *in,    \
                              void *out, size_t len, Error **errp)      \
{                                                                       \
    TYPE *ctx = container_of(cipher, TYPE, base);                       \
    if (!qcrypto_length_check(len, BLEN, errp)) {                       \
        return -1;                                                      \
    }                                                                   \
    ENCRYPT(&ctx->key, len, out, in);                                   \
    return 0;                                                           \
}                                                                       \
static int NAME##_decrypt_ecb(QCryptoCipher *cipher, const void *in,    \
                              void *out, size_t len, Error **errp)      \
{                                                                       \
    TYPE *ctx = container_of(cipher, TYPE, base);                       \
    if (!qcrypto_length_check(len, BLEN, errp)) {                       \
        return -1;                                                      \
    }                                                                   \
    DECRYPT(&ctx->key, len, out, in);                                   \
    return 0;                                                           \
}                                                                       \
static const struct QCryptoCipherDriver NAME##_driver_ecb = {           \
    .cipher_encrypt = NAME##_encrypt_ecb,                               \
    .cipher_decrypt = NAME##_decrypt_ecb,                               \
    .cipher_setiv = qcrypto_cipher_no_setiv,                            \
    .cipher_free = qcrypto_cipher_ctx_free,                             \
};


#define DEFINE_CBC(NAME, TYPE, BLEN, ENCRYPT, DECRYPT)                  \
static int NAME##_encrypt_cbc(QCryptoCipher *cipher, const void *in,    \
                              void *out, size_t len, Error **errp)      \
{                                                                       \
    TYPE *ctx = container_of(cipher, TYPE, base);                       \
    if (!qcrypto_length_check(len, BLEN, errp)) {                       \
        return -1;                                                      \
    }                                                                   \
    cbc_encrypt(&ctx->key, ENCRYPT, BLEN, ctx->iv, len, out, in);       \
    return 0;                                                           \
}                                                                       \
static int NAME##_decrypt_cbc(QCryptoCipher *cipher, const void *in,    \
                              void *out, size_t len, Error **errp)      \
{                                                                       \
    TYPE *ctx = container_of(cipher, TYPE, base);                       \
    if (!qcrypto_length_check(len, BLEN, errp)) {                       \
        return -1;                                                      \
    }                                                                   \
    cbc_decrypt(&ctx->key, DECRYPT, BLEN, ctx->iv, len, out, in);       \
    return 0;                                                           \
}                                                                       \
static const struct QCryptoCipherDriver NAME##_driver_cbc = {           \
    .cipher_encrypt = NAME##_encrypt_cbc,                               \
    .cipher_decrypt = NAME##_decrypt_cbc,                               \
    .cipher_setiv = NAME##_setiv,                                       \
    .cipher_free = qcrypto_cipher_ctx_free,                             \
};


#define DEFINE_CTR(NAME, TYPE, BLEN, ENCRYPT)                           \
static int NAME##_encrypt_ctr(QCryptoCipher *cipher, const void *in,    \
                              void *out, size_t len, Error **errp)      \
{                                                                       \
    TYPE *ctx = container_of(cipher, TYPE, base);                       \
    if (!qcrypto_length_check(len, BLEN, errp)) {                       \
        return -1;                                                      \
    }                                                                   \
    ctr_crypt(&ctx->key, ENCRYPT, BLEN, ctx->iv, len, out, in);         \
    return 0;                                                           \
}                                                                       \
static const struct QCryptoCipherDriver NAME##_driver_ctr = {           \
    .cipher_encrypt = NAME##_encrypt_ctr,                               \
    .cipher_decrypt = NAME##_encrypt_ctr,                               \
    .cipher_setiv = NAME##_setiv,                                       \
    .cipher_free = qcrypto_cipher_ctx_free,                             \
};


#ifdef CONFIG_QEMU_PRIVATE_XTS
#define DEFINE__XTS(NAME, TYPE, BLEN, ENCRYPT, DECRYPT)                 \
static void NAME##_xts_wrape(const void *ctx, size_t length,            \
                             uint8_t *dst, const uint8_t *src)          \
{                                                                       \
    ENCRYPT((const void *)ctx, length, dst, src);                       \
}                                                                       \
static void NAME##_xts_wrapd(const void *ctx, size_t length,            \
                             uint8_t *dst, const uint8_t *src)          \
{                                                                       \
    DECRYPT((const void *)ctx, length, dst, src);                       \
}                                                                       \
static int NAME##_encrypt_xts(QCryptoCipher *cipher, const void *in,    \
                              void *out, size_t len, Error **errp)      \
{                                                                       \
    TYPE *ctx = container_of(cipher, TYPE, base);                       \
    if (!qcrypto_length_check(len, BLEN, errp)) {                       \
        return -1;                                                      \
    }                                                                   \
    xts_encrypt(&ctx->key, &ctx->key_xts,                               \
                NAME##_xts_wrape, NAME##_xts_wrapd,                     \
                ctx->iv, len, out, in);                                 \
    return 0;                                                           \
}                                                                       \
static int NAME##_decrypt_xts(QCryptoCipher *cipher, const void *in,    \
                              void *out, size_t len, Error **errp)      \
{                                                                       \
    TYPE *ctx = container_of(cipher, TYPE, base);                       \
    if (!qcrypto_length_check(len, BLEN, errp)) {                       \
        return -1;                                                      \
    }                                                                   \
    xts_decrypt(&ctx->key, &ctx->key_xts,                               \
                NAME##_xts_wrape, NAME##_xts_wrapd,                     \
                ctx->iv, len, out, in);                                 \
    return 0;                                                           \
}
#else
#define DEFINE__XTS(NAME, TYPE, BLEN, ENCRYPT, DECRYPT)                 \
static int NAME##_encrypt_xts(QCryptoCipher *cipher, const void *in,    \
                              void *out, size_t len, Error **errp)      \
{                                                                       \
    TYPE *ctx = container_of(cipher, TYPE, base);                       \
    if (!qcrypto_length_check(len, BLEN, errp)) {                       \
        return -1;                                                      \
    }                                                                   \
    xts_encrypt_message(&ctx->key, &ctx->key_xts, ENCRYPT,              \
                        ctx->iv, len, out, in);                         \
    return 0;                                                           \
}                                                                       \
static int NAME##_decrypt_xts(QCryptoCipher *cipher, const void *in,    \
                              void *out, size_t len, Error **errp)      \
{                                                                       \
    TYPE *ctx = container_of(cipher, TYPE, base);                       \
    if (!qcrypto_length_check(len, BLEN, errp)) {                       \
        return -1;                                                      \
    }                                                                   \
    xts_decrypt_message(&ctx->key, &ctx->key_xts, DECRYPT, ENCRYPT,     \
                        ctx->iv, len, out, in);                         \
    return 0;                                                           \
}
#endif

#define DEFINE_XTS(NAME, TYPE, BLEN, ENCRYPT, DECRYPT)          \
    QEMU_BUILD_BUG_ON(BLEN != XTS_BLOCK_SIZE);                  \
    DEFINE__XTS(NAME, TYPE, BLEN, ENCRYPT, DECRYPT)             \
static const struct QCryptoCipherDriver NAME##_driver_xts = {   \
    .cipher_encrypt = NAME##_encrypt_xts,                       \
    .cipher_decrypt = NAME##_decrypt_xts,                       \
    .cipher_setiv = NAME##_setiv,                               \
    .cipher_free = qcrypto_cipher_ctx_free,                     \
};


#define DEFINE_ECB_CBC_CTR(NAME, TYPE, BLEN, ENCRYPT, DECRYPT)  \
    DEFINE_SETIV(NAME, TYPE, BLEN)                              \
    DEFINE_ECB(NAME, TYPE, BLEN, ENCRYPT, DECRYPT)              \
    DEFINE_CBC(NAME, TYPE, BLEN, ENCRYPT, DECRYPT)              \
    DEFINE_CTR(NAME, TYPE, BLEN, ENCRYPT)

#define DEFINE_ECB_CBC_CTR_XTS(NAME, TYPE, BLEN, ENCRYPT, DECRYPT)      \
    DEFINE_ECB_CBC_CTR(NAME, TYPE, BLEN, ENCRYPT, DECRYPT)              \
    DEFINE_XTS(NAME, TYPE, BLEN, ENCRYPT, DECRYPT)


typedef struct QCryptoNettleDES {
    QCryptoCipher base;
    struct des_ctx key;
    uint8_t iv[DES_BLOCK_SIZE];
} QCryptoNettleDES;

static void des_encrypt_native(const void *ctx, size_t length,
                               uint8_t *dst, const uint8_t *src)
{
    des_encrypt(ctx, length, dst, src);
}

static void des_decrypt_native(const void *ctx, size_t length,
                               uint8_t *dst, const uint8_t *src)
{
    des_decrypt(ctx, length, dst, src);
}

DEFINE_ECB_CBC_CTR(qcrypto_nettle_des, QCryptoNettleDES,
                   DES_BLOCK_SIZE, des_encrypt_native, des_decrypt_native)


typedef struct QCryptoNettleDES3 {
    QCryptoCipher base;
    struct des3_ctx key;
    uint8_t iv[DES3_BLOCK_SIZE];
} QCryptoNettleDES3;

static void des3_encrypt_native(const void *ctx, size_t length,
                                uint8_t *dst, const uint8_t *src)
{
    des3_encrypt(ctx, length, dst, src);
}

static void des3_decrypt_native(const void *ctx, size_t length,
                                uint8_t *dst, const uint8_t *src)
{
    des3_decrypt(ctx, length, dst, src);
}

DEFINE_ECB_CBC_CTR(qcrypto_nettle_des3, QCryptoNettleDES3, DES3_BLOCK_SIZE,
                   des3_encrypt_native, des3_decrypt_native)


typedef struct QCryptoNettleAES128 {
    QCryptoCipher base;
    uint8_t iv[AES_BLOCK_SIZE];
    /* First key from pair is encode, second key is decode. */
    struct aes128_ctx key[2], key_xts[2];
} QCryptoNettleAES128;

static void aes128_encrypt_native(const void *ctx, size_t length,
                                  uint8_t *dst, const uint8_t *src)
{
    const struct aes128_ctx *keys = ctx;
    aes128_encrypt(&keys[0], length, dst, src);
}

static void aes128_decrypt_native(const void *ctx, size_t length,
                                  uint8_t *dst, const uint8_t *src)
{
    const struct aes128_ctx *keys = ctx;
    aes128_decrypt(&keys[1], length, dst, src);
}

DEFINE_ECB_CBC_CTR_XTS(qcrypto_nettle_aes128,
                       QCryptoNettleAES128, AES_BLOCK_SIZE,
                       aes128_encrypt_native, aes128_decrypt_native)


typedef struct QCryptoNettleAES192 {
    QCryptoCipher base;
    uint8_t iv[AES_BLOCK_SIZE];
    /* First key from pair is encode, second key is decode. */
    struct aes192_ctx key[2], key_xts[2];
} QCryptoNettleAES192;

static void aes192_encrypt_native(const void *ctx, size_t length,
                                  uint8_t *dst, const uint8_t *src)
{
    const struct aes192_ctx *keys = ctx;
    aes192_encrypt(&keys[0], length, dst, src);
}

static void aes192_decrypt_native(const void *ctx, size_t length,
                                  uint8_t *dst, const uint8_t *src)
{
    const struct aes192_ctx *keys = ctx;
    aes192_decrypt(&keys[1], length, dst, src);
}

DEFINE_ECB_CBC_CTR_XTS(qcrypto_nettle_aes192,
                       QCryptoNettleAES192, AES_BLOCK_SIZE,
                       aes192_encrypt_native, aes192_decrypt_native)


typedef struct QCryptoNettleAES256 {
    QCryptoCipher base;
    uint8_t iv[AES_BLOCK_SIZE];
    /* First key from pair is encode, second key is decode. */
    struct aes256_ctx key[2], key_xts[2];
} QCryptoNettleAES256;

static void aes256_encrypt_native(const void *ctx, size_t length,
                                  uint8_t *dst, const uint8_t *src)
{
    const struct aes256_ctx *keys = ctx;
    aes256_encrypt(&keys[0], length, dst, src);
}

static void aes256_decrypt_native(const void *ctx, size_t length,
                                  uint8_t *dst, const uint8_t *src)
{
    const struct aes256_ctx *keys = ctx;
    aes256_decrypt(&keys[1], length, dst, src);
}

DEFINE_ECB_CBC_CTR_XTS(qcrypto_nettle_aes256,
                       QCryptoNettleAES256, AES_BLOCK_SIZE,
                       aes256_encrypt_native, aes256_decrypt_native)


typedef struct QCryptoNettleCAST128 {
    QCryptoCipher base;
    uint8_t iv[CAST128_BLOCK_SIZE];
    struct cast128_ctx key, key_xts;
} QCryptoNettleCAST128;

static void cast128_encrypt_native(const void *ctx, size_t length,
                                   uint8_t *dst, const uint8_t *src)
{
    cast128_encrypt(ctx, length, dst, src);
}

static void cast128_decrypt_native(const void *ctx, size_t length,
                                   uint8_t *dst, const uint8_t *src)
{
    cast128_decrypt(ctx, length, dst, src);
}

DEFINE_ECB_CBC_CTR(qcrypto_nettle_cast128,
                   QCryptoNettleCAST128, CAST128_BLOCK_SIZE,
                   cast128_encrypt_native, cast128_decrypt_native)


typedef struct QCryptoNettleSerpent {
    QCryptoCipher base;
    uint8_t iv[SERPENT_BLOCK_SIZE];
    struct serpent_ctx key, key_xts;
} QCryptoNettleSerpent;


static void serpent_encrypt_native(const void *ctx, size_t length,
                                   uint8_t *dst, const uint8_t *src)
{
    serpent_encrypt(ctx, length, dst, src);
}

static void serpent_decrypt_native(const void *ctx, size_t length,
                                   uint8_t *dst, const uint8_t *src)
{
    serpent_decrypt(ctx, length, dst, src);
}

DEFINE_ECB_CBC_CTR_XTS(qcrypto_nettle_serpent,
                       QCryptoNettleSerpent, SERPENT_BLOCK_SIZE,
                       serpent_encrypt_native, serpent_decrypt_native)


typedef struct QCryptoNettleTwofish {
    QCryptoCipher base;
    uint8_t iv[TWOFISH_BLOCK_SIZE];
    struct twofish_ctx key, key_xts;
} QCryptoNettleTwofish;

static void twofish_encrypt_native(const void *ctx, size_t length,
                                   uint8_t *dst, const uint8_t *src)
{
    twofish_encrypt(ctx, length, dst, src);
}

static void twofish_decrypt_native(const void *ctx, size_t length,
                                   uint8_t *dst, const uint8_t *src)
{
    twofish_decrypt(ctx, length, dst, src);
}

DEFINE_ECB_CBC_CTR_XTS(qcrypto_nettle_twofish,
                       QCryptoNettleTwofish, TWOFISH_BLOCK_SIZE,
                       twofish_encrypt_native, twofish_decrypt_native)

#ifdef CONFIG_CRYPTO_SM4
typedef struct QCryptoNettleSm4 {
    QCryptoCipher base;
    struct sm4_ctx key[2];
} QCryptoNettleSm4;

static void sm4_encrypt_native(void *ctx, size_t length,
                               uint8_t *dst, const uint8_t *src)
{
    struct sm4_ctx *keys = ctx;
    sm4_crypt(&keys[0], length, dst, src);
}

static void sm4_decrypt_native(void *ctx, size_t length,
                               uint8_t *dst, const uint8_t *src)
{
    struct sm4_ctx *keys = ctx;
    sm4_crypt(&keys[1], length, dst, src);
}

DEFINE_ECB(qcrypto_nettle_sm4,
           QCryptoNettleSm4, SM4_BLOCK_SIZE,
           sm4_encrypt_native, sm4_decrypt_native)
#endif

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
    case QCRYPTO_CIPHER_ALGO_TWOFISH_192:
    case QCRYPTO_CIPHER_ALGO_TWOFISH_256:
#ifdef CONFIG_CRYPTO_SM4
    case QCRYPTO_CIPHER_ALGO_SM4:
#endif
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

static QCryptoCipher *qcrypto_cipher_ctx_new(QCryptoCipherAlgo alg,
                                             QCryptoCipherMode mode,
                                             const uint8_t *key,
                                             size_t nkey,
                                             Error **errp)
{
    switch (mode) {
    case QCRYPTO_CIPHER_MODE_ECB:
    case QCRYPTO_CIPHER_MODE_CBC:
    case QCRYPTO_CIPHER_MODE_XTS:
    case QCRYPTO_CIPHER_MODE_CTR:
        break;
    default:
        goto bad_cipher_mode;
    }

    if (!qcrypto_cipher_validate_key_length(alg, mode, nkey, errp)) {
        return NULL;
    }

    switch (alg) {
    case QCRYPTO_CIPHER_ALGO_DES:
        {
            QCryptoNettleDES *ctx;
            const QCryptoCipherDriver *drv;

            switch (mode) {
            case QCRYPTO_CIPHER_MODE_ECB:
                drv = &qcrypto_nettle_des_driver_ecb;
                break;
            case QCRYPTO_CIPHER_MODE_CBC:
                drv = &qcrypto_nettle_des_driver_cbc;
                break;
            case QCRYPTO_CIPHER_MODE_CTR:
                drv = &qcrypto_nettle_des_driver_ctr;
                break;
            case QCRYPTO_CIPHER_MODE_XTS:
                goto bad_cipher_mode;
            default:
                g_assert_not_reached();
            }

            ctx = g_new0(QCryptoNettleDES, 1);
            ctx->base.driver = drv;
            des_set_key(&ctx->key, key);

            return &ctx->base;
        }

    case QCRYPTO_CIPHER_ALGO_3DES:
        {
            QCryptoNettleDES3 *ctx;
            const QCryptoCipherDriver *drv;

            switch (mode) {
            case QCRYPTO_CIPHER_MODE_ECB:
                drv = &qcrypto_nettle_des3_driver_ecb;
                break;
            case QCRYPTO_CIPHER_MODE_CBC:
                drv = &qcrypto_nettle_des3_driver_cbc;
                break;
            case QCRYPTO_CIPHER_MODE_CTR:
                drv = &qcrypto_nettle_des3_driver_ctr;
                break;
            case QCRYPTO_CIPHER_MODE_XTS:
                goto bad_cipher_mode;
            default:
                g_assert_not_reached();
            }

            ctx = g_new0(QCryptoNettleDES3, 1);
            ctx->base.driver = drv;
            des3_set_key(&ctx->key, key);
            return &ctx->base;
        }

    case QCRYPTO_CIPHER_ALGO_AES_128:
        {
            QCryptoNettleAES128 *ctx = g_new0(QCryptoNettleAES128, 1);

            switch (mode) {
            case QCRYPTO_CIPHER_MODE_ECB:
                ctx->base.driver = &qcrypto_nettle_aes128_driver_ecb;
                break;
            case QCRYPTO_CIPHER_MODE_CBC:
                ctx->base.driver = &qcrypto_nettle_aes128_driver_cbc;
                break;
            case QCRYPTO_CIPHER_MODE_CTR:
                ctx->base.driver = &qcrypto_nettle_aes128_driver_ctr;
                break;
            case QCRYPTO_CIPHER_MODE_XTS:
                ctx->base.driver = &qcrypto_nettle_aes128_driver_xts;
                nkey /= 2;
                aes128_set_encrypt_key(&ctx->key_xts[0], key + nkey);
                aes128_set_decrypt_key(&ctx->key_xts[1], key + nkey);
                break;
            default:
                g_assert_not_reached();
            }
            aes128_set_encrypt_key(&ctx->key[0], key);
            aes128_set_decrypt_key(&ctx->key[1], key);

            return &ctx->base;
        }

    case QCRYPTO_CIPHER_ALGO_AES_192:
        {
            QCryptoNettleAES192 *ctx = g_new0(QCryptoNettleAES192, 1);

            switch (mode) {
            case QCRYPTO_CIPHER_MODE_ECB:
                ctx->base.driver = &qcrypto_nettle_aes192_driver_ecb;
                break;
            case QCRYPTO_CIPHER_MODE_CBC:
                ctx->base.driver = &qcrypto_nettle_aes192_driver_cbc;
                break;
            case QCRYPTO_CIPHER_MODE_CTR:
                ctx->base.driver = &qcrypto_nettle_aes192_driver_ctr;
                break;
            case QCRYPTO_CIPHER_MODE_XTS:
                ctx->base.driver = &qcrypto_nettle_aes192_driver_xts;
                nkey /= 2;
                aes192_set_encrypt_key(&ctx->key_xts[0], key + nkey);
                aes192_set_decrypt_key(&ctx->key_xts[1], key + nkey);
                break;
            default:
                g_assert_not_reached();
            }
            aes192_set_encrypt_key(&ctx->key[0], key);
            aes192_set_decrypt_key(&ctx->key[1], key);

            return &ctx->base;
        }

    case QCRYPTO_CIPHER_ALGO_AES_256:
        {
            QCryptoNettleAES256 *ctx = g_new0(QCryptoNettleAES256, 1);

            switch (mode) {
            case QCRYPTO_CIPHER_MODE_ECB:
                ctx->base.driver = &qcrypto_nettle_aes256_driver_ecb;
                break;
            case QCRYPTO_CIPHER_MODE_CBC:
                ctx->base.driver = &qcrypto_nettle_aes256_driver_cbc;
                break;
            case QCRYPTO_CIPHER_MODE_CTR:
                ctx->base.driver = &qcrypto_nettle_aes256_driver_ctr;
                break;
            case QCRYPTO_CIPHER_MODE_XTS:
                ctx->base.driver = &qcrypto_nettle_aes256_driver_xts;
                nkey /= 2;
                aes256_set_encrypt_key(&ctx->key_xts[0], key + nkey);
                aes256_set_decrypt_key(&ctx->key_xts[1], key + nkey);
                break;
            default:
                g_assert_not_reached();
            }
            aes256_set_encrypt_key(&ctx->key[0], key);
            aes256_set_decrypt_key(&ctx->key[1], key);

            return &ctx->base;
        }

    case QCRYPTO_CIPHER_ALGO_CAST5_128:
        {
            QCryptoNettleCAST128 *ctx;
            const QCryptoCipherDriver *drv;

            switch (mode) {
            case QCRYPTO_CIPHER_MODE_ECB:
                drv = &qcrypto_nettle_cast128_driver_ecb;
                break;
            case QCRYPTO_CIPHER_MODE_CBC:
                drv = &qcrypto_nettle_cast128_driver_cbc;
                break;
            case QCRYPTO_CIPHER_MODE_CTR:
                drv = &qcrypto_nettle_cast128_driver_ctr;
                break;
            case QCRYPTO_CIPHER_MODE_XTS:
                goto bad_cipher_mode;
            default:
                g_assert_not_reached();
            }

            ctx = g_new0(QCryptoNettleCAST128, 1);
            ctx->base.driver = drv;
            cast5_set_key(&ctx->key, nkey, key);

            return &ctx->base;
        }

    case QCRYPTO_CIPHER_ALGO_SERPENT_128:
    case QCRYPTO_CIPHER_ALGO_SERPENT_192:
    case QCRYPTO_CIPHER_ALGO_SERPENT_256:
        {
            QCryptoNettleSerpent *ctx = g_new0(QCryptoNettleSerpent, 1);

            switch (mode) {
            case QCRYPTO_CIPHER_MODE_ECB:
                ctx->base.driver = &qcrypto_nettle_serpent_driver_ecb;
                break;
            case QCRYPTO_CIPHER_MODE_CBC:
                ctx->base.driver = &qcrypto_nettle_serpent_driver_cbc;
                break;
            case QCRYPTO_CIPHER_MODE_CTR:
                ctx->base.driver = &qcrypto_nettle_serpent_driver_ctr;
                break;
            case QCRYPTO_CIPHER_MODE_XTS:
                ctx->base.driver = &qcrypto_nettle_serpent_driver_xts;
                nkey /= 2;
                serpent_set_key(&ctx->key_xts, nkey, key + nkey);
                break;
            default:
                g_assert_not_reached();
            }
            serpent_set_key(&ctx->key, nkey, key);

            return &ctx->base;
        }

    case QCRYPTO_CIPHER_ALGO_TWOFISH_128:
    case QCRYPTO_CIPHER_ALGO_TWOFISH_192:
    case QCRYPTO_CIPHER_ALGO_TWOFISH_256:
        {
            QCryptoNettleTwofish *ctx = g_new0(QCryptoNettleTwofish, 1);

            switch (mode) {
            case QCRYPTO_CIPHER_MODE_ECB:
                ctx->base.driver = &qcrypto_nettle_twofish_driver_ecb;
                break;
            case QCRYPTO_CIPHER_MODE_CBC:
                ctx->base.driver = &qcrypto_nettle_twofish_driver_cbc;
                break;
            case QCRYPTO_CIPHER_MODE_CTR:
                ctx->base.driver = &qcrypto_nettle_twofish_driver_ctr;
                break;
            case QCRYPTO_CIPHER_MODE_XTS:
                ctx->base.driver = &qcrypto_nettle_twofish_driver_xts;
                nkey /= 2;
                twofish_set_key(&ctx->key_xts, nkey, key + nkey);
                break;
            default:
                g_assert_not_reached();
            }
            twofish_set_key(&ctx->key, nkey, key);

            return &ctx->base;
        }
#ifdef CONFIG_CRYPTO_SM4
    case QCRYPTO_CIPHER_ALGO_SM4:
        {
            QCryptoNettleSm4 *ctx;
            const QCryptoCipherDriver *drv;

            switch (mode) {
            case QCRYPTO_CIPHER_MODE_ECB:
                drv = &qcrypto_nettle_sm4_driver_ecb;
                break;
            case QCRYPTO_CIPHER_MODE_CBC:
            case QCRYPTO_CIPHER_MODE_CTR:
            case QCRYPTO_CIPHER_MODE_XTS:
                goto bad_cipher_mode;
            default:
                g_assert_not_reached();
            }

            ctx = g_new0(QCryptoNettleSm4, 1);
            ctx->base.driver = drv;
            sm4_set_encrypt_key(&ctx->key[0], key);
            sm4_set_decrypt_key(&ctx->key[1], key);

            return &ctx->base;
        }
#endif

    default:
        error_setg(errp, "Unsupported cipher algorithm %s",
                   QCryptoCipherAlgo_str(alg));
        return NULL;
    }

 bad_cipher_mode:
    error_setg(errp, "Unsupported cipher mode %s",
               QCryptoCipherMode_str(mode));
    return NULL;
}
