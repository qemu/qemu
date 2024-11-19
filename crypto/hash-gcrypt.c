/*
 * QEMU Crypto hash algorithms
 *
 * Copyright (c) 2024 Seagate Technology LLC and/or its Affiliates
 * Copyright (c) 2016 Red Hat, Inc.
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
#include <gcrypt.h>
#include "qapi/error.h"
#include "crypto/hash.h"
#include "hashpriv.h"


static int qcrypto_hash_alg_map[QCRYPTO_HASH_ALGO__MAX] = {
    [QCRYPTO_HASH_ALGO_MD5] = GCRY_MD_MD5,
    [QCRYPTO_HASH_ALGO_SHA1] = GCRY_MD_SHA1,
    [QCRYPTO_HASH_ALGO_SHA224] = GCRY_MD_SHA224,
    [QCRYPTO_HASH_ALGO_SHA256] = GCRY_MD_SHA256,
    [QCRYPTO_HASH_ALGO_SHA384] = GCRY_MD_SHA384,
    [QCRYPTO_HASH_ALGO_SHA512] = GCRY_MD_SHA512,
    [QCRYPTO_HASH_ALGO_RIPEMD160] = GCRY_MD_RMD160,
#ifdef CONFIG_CRYPTO_SM3
    [QCRYPTO_HASH_ALGO_SM3] = GCRY_MD_SM3,
#endif
};

gboolean qcrypto_hash_supports(QCryptoHashAlgo alg)
{
    if (alg < G_N_ELEMENTS(qcrypto_hash_alg_map) &&
        qcrypto_hash_alg_map[alg] != GCRY_MD_NONE) {
        return gcry_md_test_algo(qcrypto_hash_alg_map[alg]) == 0;
    }
    return false;
}

static
QCryptoHash *qcrypto_gcrypt_hash_new(QCryptoHashAlgo alg, Error **errp)
{
    QCryptoHash *hash;
    gcry_error_t ret;

    hash = g_new(QCryptoHash, 1);
    hash->alg = alg;
    hash->opaque = g_new(gcry_md_hd_t, 1);

    ret = gcry_md_open((gcry_md_hd_t *) hash->opaque,
                       qcrypto_hash_alg_map[alg], 0);
    if (ret != 0) {
        error_setg(errp,
                   "Unable to initialize hash algorithm: %s",
                   gcry_strerror(ret));
        g_free(hash->opaque);
        g_free(hash);
        return NULL;
    }
    return hash;
}

static
void qcrypto_gcrypt_hash_free(QCryptoHash *hash)
{
    gcry_md_hd_t *ctx = hash->opaque;

    if (ctx) {
        gcry_md_close(*ctx);
        g_free(ctx);
    }

    g_free(hash);
}


static
int qcrypto_gcrypt_hash_update(QCryptoHash *hash,
                               const struct iovec *iov,
                               size_t niov,
                               Error **errp)
{
    gcry_md_hd_t *ctx = hash->opaque;

    for (int i = 0; i < niov; i++) {
        gcry_md_write(*ctx, iov[i].iov_base, iov[i].iov_len);
    }

    return 0;
}

static
int qcrypto_gcrypt_hash_finalize(QCryptoHash *hash,
                                 uint8_t **result,
                                 size_t *result_len,
                                 Error **errp)
{
    int ret;
    unsigned char *digest;
    gcry_md_hd_t *ctx = hash->opaque;

    ret = gcry_md_get_algo_dlen(qcrypto_hash_alg_map[hash->alg]);
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

    /* Digest is freed by gcry_md_close(), copy it */
    digest = gcry_md_read(*ctx, 0);
    memcpy(*result, digest, *result_len);
    return 0;
}

QCryptoHashDriver qcrypto_hash_lib_driver = {
    .hash_new      = qcrypto_gcrypt_hash_new,
    .hash_update   = qcrypto_gcrypt_hash_update,
    .hash_finalize = qcrypto_gcrypt_hash_finalize,
    .hash_free     = qcrypto_gcrypt_hash_free,
};
