/*
 * QEMU Crypto hash algorithms
 *
 * Copyright (c) 2024 Seagate Technology LLC and/or its Affiliates
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
#include <gnutls/crypto.h>
#include "qapi/error.h"
#include "crypto/hash.h"
#include "hashpriv.h"


static int qcrypto_hash_alg_map[QCRYPTO_HASH_ALGO__MAX] = {
    [QCRYPTO_HASH_ALGO_MD5] = GNUTLS_DIG_MD5,
    [QCRYPTO_HASH_ALGO_SHA1] = GNUTLS_DIG_SHA1,
    [QCRYPTO_HASH_ALGO_SHA224] = GNUTLS_DIG_SHA224,
    [QCRYPTO_HASH_ALGO_SHA256] = GNUTLS_DIG_SHA256,
    [QCRYPTO_HASH_ALGO_SHA384] = GNUTLS_DIG_SHA384,
    [QCRYPTO_HASH_ALGO_SHA512] = GNUTLS_DIG_SHA512,
    [QCRYPTO_HASH_ALGO_RIPEMD160] = GNUTLS_DIG_RMD160,
};

gboolean qcrypto_hash_supports(QCryptoHashAlgo alg)
{
    size_t i;
    const gnutls_digest_algorithm_t *algs;
    if (alg >= G_N_ELEMENTS(qcrypto_hash_alg_map) ||
        qcrypto_hash_alg_map[alg] == GNUTLS_DIG_UNKNOWN) {
        return false;
    }
    algs = gnutls_digest_list();
    for (i = 0; algs[i] != GNUTLS_DIG_UNKNOWN; i++) {
        if (algs[i] == qcrypto_hash_alg_map[alg]) {
            return true;
        }
    }
    return false;
}

static
QCryptoHash *qcrypto_gnutls_hash_new(QCryptoHashAlgo alg, Error **errp)
{
    QCryptoHash *hash;
    int ret;

    hash = g_new(QCryptoHash, 1);
    hash->alg = alg;
    hash->opaque = g_new(gnutls_hash_hd_t, 1);

    ret = gnutls_hash_init(hash->opaque, qcrypto_hash_alg_map[alg]);
    if (ret < 0) {
        error_setg(errp,
                   "Unable to initialize hash algorithm: %s",
                   gnutls_strerror(ret));
        g_free(hash->opaque);
        g_free(hash);
        return NULL;
    }

    return hash;
}

static
void qcrypto_gnutls_hash_free(QCryptoHash *hash)
{
    gnutls_hash_hd_t *ctx = hash->opaque;

    gnutls_hash_deinit(*ctx, NULL);
    g_free(ctx);
    g_free(hash);
}


static
int qcrypto_gnutls_hash_update(QCryptoHash *hash,
                               const struct iovec *iov,
                               size_t niov,
                               Error **errp)
{
    int ret = 0;
    gnutls_hash_hd_t *ctx = hash->opaque;

    for (int i = 0; i < niov; i++) {
        ret = gnutls_hash(*ctx, iov[i].iov_base, iov[i].iov_len);
        if (ret != 0) {
            error_setg(errp, "Failed to hash data: %s",
                       gnutls_strerror(ret));
            return -1;
        }
    }

    return 0;
}

static
int qcrypto_gnutls_hash_finalize(QCryptoHash *hash,
                                 uint8_t **result,
                                 size_t *result_len,
                                 Error **errp)
{
    gnutls_hash_hd_t *ctx = hash->opaque;
    int ret;

    ret = gnutls_hash_get_len(qcrypto_hash_alg_map[hash->alg]);
    if (ret == 0) {
        error_setg(errp, "Unable to get hash length");
        return -1;
    }

    if (*result_len == 0) {
        *result_len = ret;
        *result = g_new(uint8_t, *result_len);
    } else if (*result_len != ret) {
        error_setg(errp,
                   "Result buffer size %zu is smaller than hash %d",
                   *result_len, ret);
        return -1;
    }

    gnutls_hash_output(*ctx, *result);
    return 0;
}

QCryptoHashDriver qcrypto_hash_lib_driver = {
    .hash_new      = qcrypto_gnutls_hash_new,
    .hash_update   = qcrypto_gnutls_hash_update,
    .hash_finalize = qcrypto_gnutls_hash_finalize,
    .hash_free     = qcrypto_gnutls_hash_free,
};
