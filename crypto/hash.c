/*
 * QEMU Crypto hash algorithms
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
#ifdef CONFIG_AF_ALG
    int ret;
    /*
     * TODO:
     * Maybe we should treat some afalg errors as fatal
     */
    ret = qcrypto_hash_afalg_driver.hash_bytesv(alg, iov, niov,
                                                result, resultlen,
                                                NULL);
    if (ret == 0) {
        return ret;
    }
#endif

    return qcrypto_hash_lib_driver.hash_bytesv(alg, iov, niov,
                                               result, resultlen,
                                               errp);
}


int qcrypto_hash_bytes(QCryptoHashAlgo alg,
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

int qcrypto_hash_digestv(QCryptoHashAlgo alg,
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

int qcrypto_hash_digest(QCryptoHashAlgo alg,
                        const char *buf,
                        size_t len,
                        char **digest,
                        Error **errp)
{
    struct iovec iov = { .iov_base = (char *)buf, .iov_len = len };

    return qcrypto_hash_digestv(alg, &iov, 1, digest, errp);
}

int qcrypto_hash_base64v(QCryptoHashAlgo alg,
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

int qcrypto_hash_base64(QCryptoHashAlgo alg,
                        const char *buf,
                        size_t len,
                        char **base64,
                        Error **errp)
{
    struct iovec iov = { .iov_base = (char *)buf, .iov_len = len };

    return qcrypto_hash_base64v(alg, &iov, 1, base64, errp);
}
