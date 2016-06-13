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

#ifdef CONFIG_GNUTLS_HASH
#include <gnutls/gnutls.h>
#include <gnutls/crypto.h>
#endif


static size_t qcrypto_hash_alg_size[QCRYPTO_HASH_ALG__MAX] = {
    [QCRYPTO_HASH_ALG_MD5] = 16,
    [QCRYPTO_HASH_ALG_SHA1] = 20,
    [QCRYPTO_HASH_ALG_SHA256] = 32,
};

size_t qcrypto_hash_digest_len(QCryptoHashAlgorithm alg)
{
    assert(alg < G_N_ELEMENTS(qcrypto_hash_alg_size));
    return qcrypto_hash_alg_size[alg];
}


#ifdef CONFIG_GNUTLS_HASH
static int qcrypto_hash_alg_map[QCRYPTO_HASH_ALG__MAX] = {
    [QCRYPTO_HASH_ALG_MD5] = GNUTLS_DIG_MD5,
    [QCRYPTO_HASH_ALG_SHA1] = GNUTLS_DIG_SHA1,
    [QCRYPTO_HASH_ALG_SHA256] = GNUTLS_DIG_SHA256,
};

gboolean qcrypto_hash_supports(QCryptoHashAlgorithm alg)
{
    if (alg < G_N_ELEMENTS(qcrypto_hash_alg_map)) {
        return true;
    }
    return false;
}


int qcrypto_hash_bytesv(QCryptoHashAlgorithm alg,
                        const struct iovec *iov,
                        size_t niov,
                        uint8_t **result,
                        size_t *resultlen,
                        Error **errp)
{
    int i, ret;
    gnutls_hash_hd_t dig;

    if (alg >= G_N_ELEMENTS(qcrypto_hash_alg_map)) {
        error_setg(errp,
                   "Unknown hash algorithm %d",
                   alg);
        return -1;
    }

    ret = gnutls_hash_init(&dig, qcrypto_hash_alg_map[alg]);

    if (ret < 0) {
        error_setg(errp,
                   "Unable to initialize hash algorithm: %s",
                   gnutls_strerror(ret));
        return -1;
    }

    for (i = 0; i < niov; i++) {
        ret = gnutls_hash(dig, iov[i].iov_base, iov[i].iov_len);
        if (ret < 0) {
            error_setg(errp,
                       "Unable process hash data: %s",
                       gnutls_strerror(ret));
            goto error;
        }
    }

    ret = gnutls_hash_get_len(qcrypto_hash_alg_map[alg]);
    if (ret <= 0) {
        error_setg(errp,
                   "Unable to get hash length: %s",
                   gnutls_strerror(ret));
        goto error;
    }
    if (*resultlen == 0) {
        *resultlen = ret;
        *result = g_new0(uint8_t, *resultlen);
    } else if (*resultlen != ret) {
        error_setg(errp,
                   "Result buffer size %zu is smaller than hash %d",
                   *resultlen, ret);
        goto error;
    }

    gnutls_hash_deinit(dig, *result);
    return 0;

 error:
    gnutls_hash_deinit(dig, NULL);
    return -1;
}

#else /* ! CONFIG_GNUTLS_HASH */

gboolean qcrypto_hash_supports(QCryptoHashAlgorithm alg G_GNUC_UNUSED)
{
    return false;
}

int qcrypto_hash_bytesv(QCryptoHashAlgorithm alg,
                        const struct iovec *iov G_GNUC_UNUSED,
                        size_t niov G_GNUC_UNUSED,
                        uint8_t **result G_GNUC_UNUSED,
                        size_t *resultlen G_GNUC_UNUSED,
                        Error **errp)
{
    error_setg(errp,
               "Hash algorithm %d not supported without GNUTLS",
               alg);
    return -1;
}

#endif /* ! CONFIG_GNUTLS_HASH */

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
