/*
 * QEMU Crypto akcipher algorithms
 *
 * Copyright (c) 2022 Bytedance
 * Author: lei he <helei.sig11@bytedance.com>
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

#include <nettle/rsa.h>

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "crypto/akcipher.h"
#include "crypto/random.h"
#include "qapi/error.h"
#include "sysemu/cryptodev.h"
#include "rsakey.h"

typedef struct QCryptoNettleRSA {
    QCryptoAkCipher akcipher;
    struct rsa_public_key pub;
    struct rsa_private_key priv;
    QCryptoRSAPaddingAlgorithm padding_alg;
    QCryptoHashAlgorithm hash_alg;
} QCryptoNettleRSA;

static void qcrypto_nettle_rsa_free(QCryptoAkCipher *akcipher)
{
    QCryptoNettleRSA *rsa = (QCryptoNettleRSA *)akcipher;
    if (!rsa) {
        return;
    }

    rsa_public_key_clear(&rsa->pub);
    rsa_private_key_clear(&rsa->priv);
    g_free(rsa);
}

static QCryptoAkCipher *qcrypto_nettle_rsa_new(
    const QCryptoAkCipherOptionsRSA *opt,
    QCryptoAkCipherKeyType type,
    const uint8_t *key,  size_t keylen,
    Error **errp);

QCryptoAkCipher *qcrypto_akcipher_new(const QCryptoAkCipherOptions *opts,
                                      QCryptoAkCipherKeyType type,
                                      const uint8_t *key, size_t keylen,
                                      Error **errp)
{
    switch (opts->alg) {
    case QCRYPTO_AKCIPHER_ALG_RSA:
        return qcrypto_nettle_rsa_new(&opts->u.rsa, type, key, keylen, errp);

    default:
        error_setg(errp, "Unsupported algorithm: %u", opts->alg);
        return NULL;
    }

    return NULL;
}

static void qcrypto_nettle_rsa_set_akcipher_size(QCryptoAkCipher *akcipher,
                                                 int key_size)
{
    akcipher->max_plaintext_len = key_size;
    akcipher->max_ciphertext_len = key_size;
    akcipher->max_signature_len = key_size;
    akcipher->max_dgst_len = key_size;
}

static int qcrypt_nettle_parse_rsa_private_key(QCryptoNettleRSA *rsa,
                                               const uint8_t *key,
                                               size_t keylen,
                                               Error **errp)
{
    g_autoptr(QCryptoAkCipherRSAKey) rsa_key = qcrypto_akcipher_rsakey_parse(
        QCRYPTO_AKCIPHER_KEY_TYPE_PRIVATE, key, keylen, errp);

    if (!rsa_key) {
        return -1;
    }

    nettle_mpz_init_set_str_256_u(rsa->pub.n, rsa_key->n.len, rsa_key->n.data);
    nettle_mpz_init_set_str_256_u(rsa->pub.e, rsa_key->e.len, rsa_key->e.data);
    nettle_mpz_init_set_str_256_u(rsa->priv.d, rsa_key->d.len, rsa_key->d.data);
    nettle_mpz_init_set_str_256_u(rsa->priv.p, rsa_key->p.len, rsa_key->p.data);
    nettle_mpz_init_set_str_256_u(rsa->priv.q, rsa_key->q.len, rsa_key->q.data);
    nettle_mpz_init_set_str_256_u(rsa->priv.a, rsa_key->dp.len,
                                  rsa_key->dp.data);
    nettle_mpz_init_set_str_256_u(rsa->priv.b, rsa_key->dq.len,
                                  rsa_key->dq.data);
    nettle_mpz_init_set_str_256_u(rsa->priv.c, rsa_key->u.len, rsa_key->u.data);

    if (!rsa_public_key_prepare(&rsa->pub)) {
        error_setg(errp, "Failed to check RSA key");
        return -1;
    }

    /**
     * Since in the kernel's unit test, the p, q, a, b, c of some
     * private keys is 0, only the simplest length check is done here
     */
    if (rsa_key->p.len > 1 &&
        rsa_key->q.len > 1 &&
        rsa_key->dp.len > 1 &&
        rsa_key->dq.len > 1 &&
        rsa_key->u.len > 1) {
        if (!rsa_private_key_prepare(&rsa->priv)) {
            error_setg(errp, "Failed to check RSA key");
            return -1;
        }
    } else {
        rsa->priv.size = rsa->pub.size;
    }
    qcrypto_nettle_rsa_set_akcipher_size(
        (QCryptoAkCipher *)rsa, rsa->priv.size);

    return 0;
}

static int qcrypt_nettle_parse_rsa_public_key(QCryptoNettleRSA *rsa,
                                              const uint8_t *key,
                                              size_t keylen,
                                              Error **errp)
{
    g_autoptr(QCryptoAkCipherRSAKey) rsa_key = qcrypto_akcipher_rsakey_parse(
        QCRYPTO_AKCIPHER_KEY_TYPE_PUBLIC, key, keylen, errp);

    if (!rsa_key) {
        return -1;
    }
    nettle_mpz_init_set_str_256_u(rsa->pub.n, rsa_key->n.len, rsa_key->n.data);
    nettle_mpz_init_set_str_256_u(rsa->pub.e, rsa_key->e.len, rsa_key->e.data);

    if (!rsa_public_key_prepare(&rsa->pub)) {
        error_setg(errp, "Failed to check RSA key");
        return -1;
    }
    qcrypto_nettle_rsa_set_akcipher_size(
        (QCryptoAkCipher *)rsa, rsa->pub.size);

    return 0;
}

static void wrap_nettle_random_func(void *ctx, size_t len, uint8_t *out)
{
    qcrypto_random_bytes(out, len, &error_abort);
}

static int qcrypto_nettle_rsa_encrypt(QCryptoAkCipher *akcipher,
                                      const void *data, size_t data_len,
                                      void *enc, size_t enc_len,
                                      Error **errp)
{

    QCryptoNettleRSA *rsa = (QCryptoNettleRSA *)akcipher;
    mpz_t c;
    int ret = -1;

    if (data_len > rsa->pub.size) {
        error_setg(errp, "Plaintext length %zu is greater than key size: %zu",
                   data_len, rsa->pub.size);
        return ret;
    }

    if (enc_len < rsa->pub.size) {
        error_setg(errp, "Ciphertext buffer length %zu is less than "
                         "key size: %zu", enc_len, rsa->pub.size);
        return ret;
    }

    /* Nettle do not support RSA encryption without any padding */
    switch (rsa->padding_alg) {
    case QCRYPTO_RSA_PADDING_ALG_RAW:
        error_setg(errp, "RSA with raw padding is not supported");
        break;

    case QCRYPTO_RSA_PADDING_ALG_PKCS1:
        mpz_init(c);
        if (rsa_encrypt(&rsa->pub, NULL, wrap_nettle_random_func,
                        data_len, (uint8_t *)data, c) != 1) {
            error_setg(errp, "Failed to encrypt");
        } else {
            nettle_mpz_get_str_256(enc_len, (uint8_t *)enc, c);
            ret = nettle_mpz_sizeinbase_256_u(c);
        }
        mpz_clear(c);
        break;

    default:
        error_setg(errp, "Unknown padding");
    }

    return ret;
}

static int qcrypto_nettle_rsa_decrypt(QCryptoAkCipher *akcipher,
                                      const void *enc, size_t enc_len,
                                      void *data, size_t data_len,
                                      Error **errp)
{
    QCryptoNettleRSA *rsa = (QCryptoNettleRSA *)akcipher;
    mpz_t c;
    int ret = -1;

    if (enc_len > rsa->priv.size) {
        error_setg(errp, "Ciphertext length %zu is greater than key size: %zu",
                   enc_len, rsa->priv.size);
        return ret;
    }

    switch (rsa->padding_alg) {
    case QCRYPTO_RSA_PADDING_ALG_RAW:
        error_setg(errp, "RSA with raw padding is not supported");
        break;

    case QCRYPTO_RSA_PADDING_ALG_PKCS1:
        nettle_mpz_init_set_str_256_u(c, enc_len, enc);
        if (!rsa_decrypt(&rsa->priv, &data_len, (uint8_t *)data, c)) {
            error_setg(errp, "Failed to decrypt");
        } else {
            ret = data_len;
        }

        mpz_clear(c);
        break;

    default:
        error_setg(errp, "Unknown padding algorithm: %d", rsa->padding_alg);
    }

    return ret;
}

static int qcrypto_nettle_rsa_sign(QCryptoAkCipher *akcipher,
                                   const void *data, size_t data_len,
                                   void *sig, size_t sig_len, Error **errp)
{
    QCryptoNettleRSA *rsa = (QCryptoNettleRSA *)akcipher;
    int ret = -1, rv;
    mpz_t s;

    /**
     * The RSA algorithm cannot be used for signature/verification
     * without padding.
     */
    if (rsa->padding_alg == QCRYPTO_RSA_PADDING_ALG_RAW) {
        error_setg(errp, "Try to make signature without padding");
        return ret;
    }

    if (data_len > rsa->priv.size) {
        error_setg(errp, "Data length %zu is greater than key size: %zu",
                   data_len, rsa->priv.size);
        return ret;
    }

    if (sig_len < rsa->priv.size) {
        error_setg(errp, "Signature buffer length %zu is less than "
                         "key size: %zu", sig_len, rsa->priv.size);
        return ret;
    }

    mpz_init(s);
    switch (rsa->hash_alg) {
    case QCRYPTO_HASH_ALG_MD5:
        rv = rsa_md5_sign_digest(&rsa->priv, data, s);
        break;

    case QCRYPTO_HASH_ALG_SHA1:
        rv = rsa_sha1_sign_digest(&rsa->priv, data, s);
        break;

    case QCRYPTO_HASH_ALG_SHA256:
        rv = rsa_sha256_sign_digest(&rsa->priv, data, s);
        break;

    case QCRYPTO_HASH_ALG_SHA512:
        rv = rsa_sha512_sign_digest(&rsa->priv, data, s);
        break;

    default:
        error_setg(errp, "Unknown hash algorithm: %d", rsa->hash_alg);
        goto cleanup;
    }

    if (rv != 1) {
        error_setg(errp, "Failed to make signature");
        goto cleanup;
    }
    nettle_mpz_get_str_256(sig_len, (uint8_t *)sig, s);
    ret = nettle_mpz_sizeinbase_256_u(s);

cleanup:
    mpz_clear(s);

    return ret;
}

static int qcrypto_nettle_rsa_verify(QCryptoAkCipher *akcipher,
                                     const void *sig, size_t sig_len,
                                     const void *data, size_t data_len,
                                     Error **errp)
{
    QCryptoNettleRSA *rsa = (QCryptoNettleRSA *)akcipher;

    int ret = -1, rv;
    mpz_t s;

    /**
     * The RSA algorithm cannot be used for signature/verification
     * without padding.
     */
    if (rsa->padding_alg == QCRYPTO_RSA_PADDING_ALG_RAW) {
        error_setg(errp, "Try to verify signature without padding");
        return ret;
    }
    if (data_len > rsa->pub.size) {
        error_setg(errp, "Data length %zu is greater than key size: %zu",
                   data_len, rsa->pub.size);
        return ret;
    }
    if (sig_len < rsa->pub.size) {
        error_setg(errp, "Signature length %zu is greater than key size: %zu",
                   sig_len, rsa->pub.size);
        return ret;
    }

    nettle_mpz_init_set_str_256_u(s, sig_len, sig);
    switch (rsa->hash_alg) {
    case QCRYPTO_HASH_ALG_MD5:
        rv = rsa_md5_verify_digest(&rsa->pub, data, s);
        break;

    case QCRYPTO_HASH_ALG_SHA1:
        rv = rsa_sha1_verify_digest(&rsa->pub, data, s);
        break;

    case QCRYPTO_HASH_ALG_SHA256:
        rv = rsa_sha256_verify_digest(&rsa->pub, data, s);
        break;

    case QCRYPTO_HASH_ALG_SHA512:
        rv = rsa_sha512_verify_digest(&rsa->pub, data, s);
        break;

    default:
        error_setg(errp, "Unsupported hash algorithm: %d", rsa->hash_alg);
        goto cleanup;
    }

    if (rv != 1) {
        error_setg(errp, "Failed to verify signature");
        goto cleanup;
    }
    ret = 0;

cleanup:
    mpz_clear(s);

    return ret;
}

QCryptoAkCipherDriver nettle_rsa = {
    .encrypt = qcrypto_nettle_rsa_encrypt,
    .decrypt = qcrypto_nettle_rsa_decrypt,
    .sign = qcrypto_nettle_rsa_sign,
    .verify = qcrypto_nettle_rsa_verify,
    .free = qcrypto_nettle_rsa_free,
};

static QCryptoAkCipher *qcrypto_nettle_rsa_new(
    const QCryptoAkCipherOptionsRSA *opt,
    QCryptoAkCipherKeyType type,
    const uint8_t *key, size_t keylen,
    Error **errp)
{
    QCryptoNettleRSA *rsa = g_new0(QCryptoNettleRSA, 1);

    rsa->padding_alg = opt->padding_alg;
    rsa->hash_alg = opt->hash_alg;
    rsa->akcipher.driver = &nettle_rsa;
    rsa_public_key_init(&rsa->pub);
    rsa_private_key_init(&rsa->priv);

    switch (type) {
    case QCRYPTO_AKCIPHER_KEY_TYPE_PRIVATE:
        if (qcrypt_nettle_parse_rsa_private_key(rsa, key, keylen, errp) != 0) {
            goto error;
        }
        break;

    case QCRYPTO_AKCIPHER_KEY_TYPE_PUBLIC:
        if (qcrypt_nettle_parse_rsa_public_key(rsa, key, keylen, errp) != 0) {
            goto error;
        }
        break;

    default:
        error_setg(errp, "Unknown akcipher key type %d", type);
        goto error;
    }

    return (QCryptoAkCipher *)rsa;

error:
    qcrypto_nettle_rsa_free((QCryptoAkCipher *)rsa);
    return NULL;
}


bool qcrypto_akcipher_supports(QCryptoAkCipherOptions *opts)
{
    switch (opts->alg) {
    case QCRYPTO_AKCIPHER_ALG_RSA:
        switch (opts->u.rsa.padding_alg) {
        case QCRYPTO_RSA_PADDING_ALG_PKCS1:
            switch (opts->u.rsa.hash_alg) {
            case QCRYPTO_HASH_ALG_MD5:
            case QCRYPTO_HASH_ALG_SHA1:
            case QCRYPTO_HASH_ALG_SHA256:
            case QCRYPTO_HASH_ALG_SHA512:
                return true;

            default:
                return false;
            }

        case QCRYPTO_RSA_PADDING_ALG_RAW:
        default:
            return false;
        }
        break;

    default:
        return false;
    }
}
