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
#include "crypto/desrfb.h"
#include "crypto/xts.h"

static const struct QCryptoCipherDriver qcrypto_cipher_lib_driver;

typedef struct QCryptoCipherBuiltinAESContext QCryptoCipherBuiltinAESContext;
struct QCryptoCipherBuiltinAESContext {
    AES_KEY enc;
    AES_KEY dec;
};
typedef struct QCryptoCipherBuiltinAES QCryptoCipherBuiltinAES;
struct QCryptoCipherBuiltinAES {
    QCryptoCipherBuiltinAESContext key;
    QCryptoCipherBuiltinAESContext key_tweak;
    uint8_t iv[AES_BLOCK_SIZE];
};
typedef struct QCryptoCipherBuiltinDESRFB QCryptoCipherBuiltinDESRFB;
struct QCryptoCipherBuiltinDESRFB {
    uint8_t *key;
    size_t nkey;
};

typedef struct QCryptoCipherBuiltin QCryptoCipherBuiltin;
struct QCryptoCipherBuiltin {
    QCryptoCipher base;

    union {
        QCryptoCipherBuiltinAES aes;
        QCryptoCipherBuiltinDESRFB desrfb;
    } state;
    size_t blocksize;
    void (*free)(QCryptoCipher *cipher);
    int (*setiv)(QCryptoCipher *cipher,
                 const uint8_t *iv, size_t niv,
                 Error **errp);
    int (*encrypt)(QCryptoCipher *cipher,
                   const void *in,
                   void *out,
                   size_t len,
                   Error **errp);
    int (*decrypt)(QCryptoCipher *cipher,
                   const void *in,
                   void *out,
                   size_t len,
                   Error **errp);
};


static void qcrypto_cipher_free_aes(QCryptoCipher *cipher)
{
    g_free(cipher);
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

static int qcrypto_cipher_encrypt_aes(QCryptoCipher *cipher,
                                      const void *in,
                                      void *out,
                                      size_t len,
                                      Error **errp)
{
    QCryptoCipherBuiltin *ctxt
        = container_of(cipher, QCryptoCipherBuiltin, base);

    switch (cipher->mode) {
    case QCRYPTO_CIPHER_MODE_ECB:
        do_aes_encrypt_ecb(&ctxt->state.aes.key, len, out, in);
        break;
    case QCRYPTO_CIPHER_MODE_CBC:
        AES_cbc_encrypt(in, out, len,
                        &ctxt->state.aes.key.enc,
                        ctxt->state.aes.iv, 1);
        break;
    case QCRYPTO_CIPHER_MODE_XTS:
        xts_encrypt(&ctxt->state.aes.key,
                    &ctxt->state.aes.key_tweak,
                    do_aes_encrypt_ecb,
                    do_aes_decrypt_ecb,
                    ctxt->state.aes.iv,
                    len, out, in);
        break;
    default:
        g_assert_not_reached();
    }

    return 0;
}


static int qcrypto_cipher_decrypt_aes(QCryptoCipher *cipher,
                                      const void *in,
                                      void *out,
                                      size_t len,
                                      Error **errp)
{
    QCryptoCipherBuiltin *ctxt
        = container_of(cipher, QCryptoCipherBuiltin, base);

    switch (cipher->mode) {
    case QCRYPTO_CIPHER_MODE_ECB:
        do_aes_decrypt_ecb(&ctxt->state.aes.key, len, out, in);
        break;
    case QCRYPTO_CIPHER_MODE_CBC:
        AES_cbc_encrypt(in, out, len,
                        &ctxt->state.aes.key.dec,
                        ctxt->state.aes.iv, 0);
        break;
    case QCRYPTO_CIPHER_MODE_XTS:
        xts_decrypt(&ctxt->state.aes.key,
                    &ctxt->state.aes.key_tweak,
                    do_aes_encrypt_ecb,
                    do_aes_decrypt_ecb,
                    ctxt->state.aes.iv,
                    len, out, in);
        break;
    default:
        g_assert_not_reached();
    }

    return 0;
}

static int qcrypto_cipher_setiv_aes(QCryptoCipher *cipher,
                                     const uint8_t *iv, size_t niv,
                                     Error **errp)
{
    QCryptoCipherBuiltin *ctxt
        = container_of(cipher, QCryptoCipherBuiltin, base);

    if (niv != AES_BLOCK_SIZE) {
        error_setg(errp, "IV must be %d bytes not %zu",
                   AES_BLOCK_SIZE, niv);
        return -1;
    }

    memcpy(ctxt->state.aes.iv, iv, AES_BLOCK_SIZE);

    return 0;
}




static QCryptoCipher *
qcrypto_cipher_init_aes(QCryptoCipherMode mode,
                        const uint8_t *key, size_t nkey,
                        Error **errp)
{
    QCryptoCipherBuiltin *ctxt;

    if (mode != QCRYPTO_CIPHER_MODE_CBC &&
        mode != QCRYPTO_CIPHER_MODE_ECB &&
        mode != QCRYPTO_CIPHER_MODE_XTS) {
        error_setg(errp, "Unsupported cipher mode %s",
                   QCryptoCipherMode_str(mode));
        return NULL;
    }

    ctxt = g_new0(QCryptoCipherBuiltin, 1);

    if (mode == QCRYPTO_CIPHER_MODE_XTS) {
        if (AES_set_encrypt_key(key, nkey * 4, &ctxt->state.aes.key.enc) != 0) {
            error_setg(errp, "Failed to set encryption key");
            goto error;
        }

        if (AES_set_decrypt_key(key, nkey * 4, &ctxt->state.aes.key.dec) != 0) {
            error_setg(errp, "Failed to set decryption key");
            goto error;
        }

        if (AES_set_encrypt_key(key + (nkey / 2), nkey * 4,
                                &ctxt->state.aes.key_tweak.enc) != 0) {
            error_setg(errp, "Failed to set encryption key");
            goto error;
        }

        if (AES_set_decrypt_key(key + (nkey / 2), nkey * 4,
                                &ctxt->state.aes.key_tweak.dec) != 0) {
            error_setg(errp, "Failed to set decryption key");
            goto error;
        }
    } else {
        if (AES_set_encrypt_key(key, nkey * 8, &ctxt->state.aes.key.enc) != 0) {
            error_setg(errp, "Failed to set encryption key");
            goto error;
        }

        if (AES_set_decrypt_key(key, nkey * 8, &ctxt->state.aes.key.dec) != 0) {
            error_setg(errp, "Failed to set decryption key");
            goto error;
        }
    }

    ctxt->blocksize = AES_BLOCK_SIZE;
    ctxt->free = qcrypto_cipher_free_aes;
    ctxt->setiv = qcrypto_cipher_setiv_aes;
    ctxt->encrypt = qcrypto_cipher_encrypt_aes;
    ctxt->decrypt = qcrypto_cipher_decrypt_aes;

    ctxt->base.driver = &qcrypto_cipher_lib_driver;
    return &ctxt->base;

 error:
    g_free(ctxt);
    return NULL;
}


static void qcrypto_cipher_free_des_rfb(QCryptoCipher *cipher)
{
    QCryptoCipherBuiltin *ctxt
        = container_of(cipher, QCryptoCipherBuiltin, base);

    g_free(ctxt->state.desrfb.key);
    g_free(ctxt);
}


static int qcrypto_cipher_encrypt_des_rfb(QCryptoCipher *cipher,
                                          const void *in,
                                          void *out,
                                          size_t len,
                                          Error **errp)
{
    QCryptoCipherBuiltin *ctxt
        = container_of(cipher, QCryptoCipherBuiltin, base);
    size_t i;

    if (len % 8) {
        error_setg(errp, "Buffer size must be multiple of 8 not %zu",
                   len);
        return -1;
    }

    deskey(ctxt->state.desrfb.key, EN0);

    for (i = 0; i < len; i += 8) {
        des((void *)in + i, out + i);
    }

    return 0;
}


static int qcrypto_cipher_decrypt_des_rfb(QCryptoCipher *cipher,
                                          const void *in,
                                          void *out,
                                          size_t len,
                                          Error **errp)
{
    QCryptoCipherBuiltin *ctxt
        = container_of(cipher, QCryptoCipherBuiltin, base);
    size_t i;

    if (len % 8) {
        error_setg(errp, "Buffer size must be multiple of 8 not %zu",
                   len);
        return -1;
    }

    deskey(ctxt->state.desrfb.key, DE1);

    for (i = 0; i < len; i += 8) {
        des((void *)in + i, out + i);
    }

    return 0;
}


static int qcrypto_cipher_setiv_des_rfb(QCryptoCipher *cipher,
                                        const uint8_t *iv, size_t niv,
                                        Error **errp)
{
    error_setg(errp, "Setting IV is not supported");
    return -1;
}


static QCryptoCipher *
qcrypto_cipher_init_des_rfb(QCryptoCipherMode mode,
                            const uint8_t *key, size_t nkey,
                            Error **errp)
{
    QCryptoCipherBuiltin *ctxt;

    if (mode != QCRYPTO_CIPHER_MODE_ECB) {
        error_setg(errp, "Unsupported cipher mode %s",
                   QCryptoCipherMode_str(mode));
        return NULL;
    }

    ctxt = g_new0(QCryptoCipherBuiltin, 1);

    ctxt->state.desrfb.key = g_new0(uint8_t, nkey);
    memcpy(ctxt->state.desrfb.key, key, nkey);
    ctxt->state.desrfb.nkey = nkey;

    ctxt->blocksize = 8;
    ctxt->free = qcrypto_cipher_free_des_rfb;
    ctxt->setiv = qcrypto_cipher_setiv_des_rfb;
    ctxt->encrypt = qcrypto_cipher_encrypt_des_rfb;
    ctxt->decrypt = qcrypto_cipher_decrypt_des_rfb;

    ctxt->base.driver = &qcrypto_cipher_lib_driver;
    return &ctxt->base;
}


bool qcrypto_cipher_supports(QCryptoCipherAlgorithm alg,
                             QCryptoCipherMode mode)
{
    switch (alg) {
    case QCRYPTO_CIPHER_ALG_DES_RFB:
    case QCRYPTO_CIPHER_ALG_AES_128:
    case QCRYPTO_CIPHER_ALG_AES_192:
    case QCRYPTO_CIPHER_ALG_AES_256:
        break;
    default:
        return false;
    }

    switch (mode) {
    case QCRYPTO_CIPHER_MODE_ECB:
    case QCRYPTO_CIPHER_MODE_CBC:
    case QCRYPTO_CIPHER_MODE_XTS:
        return true;
    case QCRYPTO_CIPHER_MODE_CTR:
        return false;
    default:
        return false;
    }
}


static QCryptoCipher *qcrypto_cipher_ctx_new(QCryptoCipherAlgorithm alg,
                                             QCryptoCipherMode mode,
                                             const uint8_t *key,
                                             size_t nkey,
                                             Error **errp)
{
    switch (mode) {
    case QCRYPTO_CIPHER_MODE_ECB:
    case QCRYPTO_CIPHER_MODE_CBC:
    case QCRYPTO_CIPHER_MODE_XTS:
        break;
    default:
        error_setg(errp, "Unsupported cipher mode %s",
                   QCryptoCipherMode_str(mode));
        return NULL;
    }

    if (!qcrypto_cipher_validate_key_length(alg, mode, nkey, errp)) {
        return NULL;
    }

    switch (alg) {
    case QCRYPTO_CIPHER_ALG_DES_RFB:
        return qcrypto_cipher_init_des_rfb(mode, key, nkey, errp);
    case QCRYPTO_CIPHER_ALG_AES_128:
    case QCRYPTO_CIPHER_ALG_AES_192:
    case QCRYPTO_CIPHER_ALG_AES_256:
        return qcrypto_cipher_init_aes(mode, key, nkey, errp);
    default:
        error_setg(errp,
                   "Unsupported cipher algorithm %s",
                   QCryptoCipherAlgorithm_str(alg));
        return NULL;
    }
}

static void
qcrypto_builtin_cipher_ctx_free(QCryptoCipher *cipher)
{
    QCryptoCipherBuiltin *ctxt
        = container_of(cipher, QCryptoCipherBuiltin, base);

    ctxt->free(cipher);
}


static int
qcrypto_builtin_cipher_encrypt(QCryptoCipher *cipher,
                               const void *in,
                               void *out,
                               size_t len,
                               Error **errp)
{
    QCryptoCipherBuiltin *ctxt
        = container_of(cipher, QCryptoCipherBuiltin, base);

    if (len & (ctxt->blocksize - 1)) {
        error_setg(errp, "Length %zu must be a multiple of block size %zu",
                   len, ctxt->blocksize);
        return -1;
    }

    return ctxt->encrypt(cipher, in, out, len, errp);
}


static int
qcrypto_builtin_cipher_decrypt(QCryptoCipher *cipher,
                               const void *in,
                               void *out,
                               size_t len,
                               Error **errp)
{
    QCryptoCipherBuiltin *ctxt
        = container_of(cipher, QCryptoCipherBuiltin, base);

    if (len & (ctxt->blocksize - 1)) {
        error_setg(errp, "Length %zu must be a multiple of block size %zu",
                   len, ctxt->blocksize);
        return -1;
    }

    return ctxt->decrypt(cipher, in, out, len, errp);
}


static int
qcrypto_builtin_cipher_setiv(QCryptoCipher *cipher,
                             const uint8_t *iv, size_t niv,
                             Error **errp)
{
    QCryptoCipherBuiltin *ctxt
        = container_of(cipher, QCryptoCipherBuiltin, base);

    return ctxt->setiv(cipher, iv, niv, errp);
}


static const struct QCryptoCipherDriver qcrypto_cipher_lib_driver = {
    .cipher_encrypt = qcrypto_builtin_cipher_encrypt,
    .cipher_decrypt = qcrypto_builtin_cipher_decrypt,
    .cipher_setiv = qcrypto_builtin_cipher_setiv,
    .cipher_free = qcrypto_builtin_cipher_ctx_free,
};
