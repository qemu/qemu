/*
 * QEMU Crypto hash algorithms
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
#include "qapi/error.h"
#include "crypto/hash.h"
#include "hashpriv.h"

static size_t qcrypto_hash_alg_size[QCRYPTO_HASH_ALG__MAX] = {
    [QCRYPTO_HASH_ALG_MD5] = 16,
    [QCRYPTO_HASH_ALG_SHA1] = 20,
    [QCRYPTO_HASH_ALG_SHA224] = 28,
    [QCRYPTO_HASH_ALG_SHA256] = 32,
    [QCRYPTO_HASH_ALG_SHA384] = 48,
    [QCRYPTO_HASH_ALG_SHA512] = 64,
    [QCRYPTO_HASH_ALG_RIPEMD160] = 20,
};

size_t qcrypto_hash_digest_len(QCryptoHashAlgorithm alg)
{
    assert(alg < G_N_ELEMENTS(qcrypto_hash_alg_size));
    return qcrypto_hash_alg_size[alg];
}

int qcrypto_hash_bytesv(QCryptoHashAlgorithm alg,
                        const struct iovec *iov,
                        size_t niov,
                        uint8_t **result,
                        size_t *resultlen,
                        Error **errp)
{
#ifdef CONFIG_AF_ALG
    int ret;

    ret = qcrypto_hash_afalg_driver.hash_bytesv(alg, iov, niov,
                                                result, resultlen,
                                                errp);
    if (ret == 0) {
        return ret;
    }

    /*
     * TODO:
     * Maybe we should treat some afalg errors as fatal
     */
    error_free(*errp);
#endif

    return qcrypto_hash_lib_driver.hash_bytesv(alg, iov, niov,
                                               result, resultlen,
                                               errp);
}


int qcrypto_hash_bytes(QCryptoHashAlgorithm alg,
                       const char *buf,
                       size_t len,
                       uint8_t **result,
                       size_t *resultlen,
                       Error **errp)
{
    struct iovec iov = { .iov_base = (char *)buf,
                         .iov_len = len };
    return qcrypto_hash_bytesv(alg, &iov, 1, result, resultlen, errp);
}

static const char hex[] = "0123456789abcdef";

int qcrypto_hash_digestv(QCryptoHashAlgorithm alg,
                         const struct iovec *iov,
                         size_t niov,
                         char **digest,
                         Error **errp)
{
    uint8_t *result = NULL;
    size_t resultlen = 0;
    size_t i;

    if (qcrypto_hash_bytesv(alg, iov, niov, &result, &resultlen, errp) < 0) {
        return -1;
    }

    *digest = g_new0(char, (resultlen * 2) + 1);
    for (i = 0 ; i < resultlen ; i++) {
        (*digest)[(i * 2)] = hex[(result[i] >> 4) & 0xf];
        (*digest)[(i * 2) + 1] = hex[result[i] & 0xf];
    }
    (*digest)[resultlen * 2] = '\0';
    g_free(result);
    return 0;
}

int qcrypto_hash_digest(QCryptoHashAlgorithm alg,
                        const char *buf,
                        size_t len,
                        char **digest,
                        Error **errp)
{
    struct iovec iov = { .iov_base = (char *)buf, .iov_len = len };

    return qcrypto_hash_digestv(alg, &iov, 1, digest, errp);
}

int qcrypto_hash_base64v(QCryptoHashAlgorithm alg,
                         const struct iovec *iov,
                         size_t niov,
                         char **base64,
                         Error **errp)
{
    uint8_t *result = NULL;
    size_t resultlen = 0;

    if (qcrypto_hash_bytesv(alg, iov, niov, &result, &resultlen, errp) < 0) {
        return -1;
    }

    *base64 = g_base64_encode(result, resultlen);
    g_free(result);
    return 0;
}

int qcrypto_hash_base64(QCryptoHashAlgorithm alg,
                        const char *buf,
                        size_t len,
                        char **base64,
                        Error **errp)
{
    struct iovec iov = { .iov_base = (char *)buf, .iov_len = len };

    return qcrypto_hash_base64v(alg, &iov, 1, base64, errp);
}
