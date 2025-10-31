/*
 * QEMU Crypto hash algorithms
 *
 * Copyright (c) 2024 Seagate Technology LLC and/or its Affiliates
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
#include "qapi/error.h"
#include "qapi-types-crypto.h"
#include "crypto/hash.h"
#include "hashpriv.h"

static size_t qcrypto_hash_alg_size[QCRYPTO_HASH_ALGO__MAX] = {
    [QCRYPTO_HASH_ALGO_MD5]       = QCRYPTO_HASH_DIGEST_LEN_MD5,
    [QCRYPTO_HASH_ALGO_SHA1]      = QCRYPTO_HASH_DIGEST_LEN_SHA1,
    [QCRYPTO_HASH_ALGO_SHA224]    = QCRYPTO_HASH_DIGEST_LEN_SHA224,
    [QCRYPTO_HASH_ALGO_SHA256]    = QCRYPTO_HASH_DIGEST_LEN_SHA256,
    [QCRYPTO_HASH_ALGO_SHA384]    = QCRYPTO_HASH_DIGEST_LEN_SHA384,
    [QCRYPTO_HASH_ALGO_SHA512]    = QCRYPTO_HASH_DIGEST_LEN_SHA512,
    [QCRYPTO_HASH_ALGO_RIPEMD160] = QCRYPTO_HASH_DIGEST_LEN_RIPEMD160,
#ifdef CONFIG_CRYPTO_SM3
    [QCRYPTO_HASH_ALGO_SM3] = QCRYPTO_HASH_DIGEST_LEN_SM3,
#endif
};

size_t qcrypto_hash_digest_len(QCryptoHashAlgo alg)
{
    assert(alg < G_N_ELEMENTS(qcrypto_hash_alg_size));
    return qcrypto_hash_alg_size[alg];
}

int qcrypto_hash_bytesv(QCryptoHashAlgo alg,
                        const struct iovec *iov,
                        size_t niov,
                        uint8_t **result,
                        size_t *resultlen,
                        Error **errp)
{
    g_autoptr(QCryptoHash) ctx = qcrypto_hash_new(alg, errp);

    if (!ctx) {
        return -1;
    }

    if (qcrypto_hash_updatev(ctx, iov, niov, errp) < 0 ||
        qcrypto_hash_finalize_bytes(ctx, result, resultlen, errp) < 0) {
        return -1;
    }

    return 0;
}


int qcrypto_hash_bytes(QCryptoHashAlgo alg,
                       const void *buf,
                       size_t len,
                       uint8_t **result,
                       size_t *resultlen,
                       Error **errp)
{
    struct iovec iov = { .iov_base = (void *)buf,
                         .iov_len = len };
    return qcrypto_hash_bytesv(alg, &iov, 1, result, resultlen, errp);
}

int qcrypto_hash_updatev(QCryptoHash *hash,
                         const struct iovec *iov,
                         size_t niov,
                         Error **errp)
{
    QCryptoHashDriver *drv = hash->driver;

    return drv->hash_update(hash, iov, niov, errp);
}

int qcrypto_hash_update(QCryptoHash *hash,
                        const void *buf,
                        size_t len,
                        Error **errp)
{
    struct iovec iov = { .iov_base = (void *)buf, .iov_len = len };

    return qcrypto_hash_updatev(hash, &iov, 1, errp);
}

QCryptoHash *qcrypto_hash_new(QCryptoHashAlgo alg, Error **errp)
{
    QCryptoHash *hash = NULL;

    if (!qcrypto_hash_supports(alg)) {
        error_setg(errp, "Unsupported hash algorithm %s",
                   QCryptoHashAlgo_str(alg));
        return NULL;
   }

#ifdef CONFIG_AF_ALG
    hash = qcrypto_hash_afalg_driver.hash_new(alg, NULL);
    if (hash) {
        hash->driver = &qcrypto_hash_afalg_driver;
        return hash;
    }
#endif

    hash = qcrypto_hash_lib_driver.hash_new(alg, errp);
    if (!hash) {
        return NULL;
    }

    hash->driver = &qcrypto_hash_lib_driver;
    return hash;
}

void qcrypto_hash_free(QCryptoHash *hash)
{
   QCryptoHashDriver *drv;

    if (hash) {
        drv = hash->driver;
        drv->hash_free(hash);
    }
}

int qcrypto_hash_finalize_bytes(QCryptoHash *hash,
                                uint8_t **result,
                                size_t *result_len,
                                Error **errp)
{
    QCryptoHashDriver *drv = hash->driver;

    return drv->hash_finalize(hash, result, result_len, errp);
}

static const char hex[] = "0123456789abcdef";

int qcrypto_hash_finalize_digest(QCryptoHash *hash,
                                 char **digest,
                                 Error **errp)
{
    int ret;
    g_autofree uint8_t *result = NULL;
    size_t resultlen = 0;
    size_t i;

    ret = qcrypto_hash_finalize_bytes(hash, &result, &resultlen, errp);
    if (ret == 0) {
        *digest = g_new0(char, (resultlen * 2) + 1);
        for (i = 0 ; i < resultlen ; i++) {
            (*digest)[(i * 2)] = hex[(result[i] >> 4) & 0xf];
            (*digest)[(i * 2) + 1] = hex[result[i] & 0xf];
        }
        (*digest)[resultlen * 2] = '\0';
    }

    return ret;
}

int qcrypto_hash_finalize_base64(QCryptoHash *hash,
                                 char **base64,
                                 Error **errp)
{
    int ret;
    g_autofree uint8_t *result = NULL;
    size_t resultlen = 0;

    ret = qcrypto_hash_finalize_bytes(hash, &result, &resultlen, errp);
    if (ret == 0) {
        *base64 = g_base64_encode(result, resultlen);
    }

    return ret;
}

int qcrypto_hash_digestv(QCryptoHashAlgo alg,
                         const struct iovec *iov,
                         size_t niov,
                         char **digest,
                         Error **errp)
{
    g_autoptr(QCryptoHash) ctx = qcrypto_hash_new(alg, errp);

    if (!ctx) {
        return -1;
    }

    if (qcrypto_hash_updatev(ctx, iov, niov, errp) < 0 ||
        qcrypto_hash_finalize_digest(ctx, digest, errp) < 0) {
        return -1;
    }

    return 0;
}

int qcrypto_hash_digest(QCryptoHashAlgo alg,
                        const void *buf,
                        size_t len,
                        char **digest,
                        Error **errp)
{
    struct iovec iov = { .iov_base = (void *)buf, .iov_len = len };

    return qcrypto_hash_digestv(alg, &iov, 1, digest, errp);
}

int qcrypto_hash_base64v(QCryptoHashAlgo alg,
                         const struct iovec *iov,
                         size_t niov,
                         char **base64,
                         Error **errp)
{
    g_autoptr(QCryptoHash) ctx = qcrypto_hash_new(alg, errp);

    if (!ctx) {
        return -1;
    }

    if (qcrypto_hash_updatev(ctx, iov, niov, errp) < 0 ||
        qcrypto_hash_finalize_base64(ctx, base64, errp) < 0) {
        return -1;
    }

    return 0;
}

int qcrypto_hash_base64(QCryptoHashAlgo alg,
                        const void *buf,
                        size_t len,
                        char **base64,
                        Error **errp)
{
    struct iovec iov = { .iov_base = (void *)buf, .iov_len = len };

    return qcrypto_hash_base64v(alg, &iov, 1, base64, errp);
}
