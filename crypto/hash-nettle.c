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
#include "qapi/error.h"
#include "crypto/hash.h"
#include "hashpriv.h"
#include <nettle/md5.h>
#include <nettle/sha.h>
#include <nettle/ripemd160.h>
#ifdef CONFIG_CRYPTO_SM3
#include <nettle/sm3.h>
#endif

typedef void (*qcrypto_nettle_init)(void *ctx);
typedef void (*qcrypto_nettle_write)(void *ctx,
                                     size_t len,
                                     const uint8_t *buf);
typedef void (*qcrypto_nettle_result)(void *ctx,
                                      size_t len,
                                      uint8_t *buf);

union qcrypto_hash_ctx {
    struct md5_ctx md5;
    struct sha1_ctx sha1;
    struct sha224_ctx sha224;
    struct sha256_ctx sha256;
    struct sha384_ctx sha384;
    struct sha512_ctx sha512;
    struct ripemd160_ctx ripemd160;
#ifdef CONFIG_CRYPTO_SM3
    struct sm3_ctx sm3;
#endif
};

struct qcrypto_hash_alg {
    qcrypto_nettle_init init;
    qcrypto_nettle_write write;
    qcrypto_nettle_result result;
    size_t len;
} qcrypto_hash_alg_map[] = {
    [QCRYPTO_HASH_ALGO_MD5] = {
        .init = (qcrypto_nettle_init)md5_init,
        .write = (qcrypto_nettle_write)md5_update,
        .result = (qcrypto_nettle_result)md5_digest,
        .len = MD5_DIGEST_SIZE,
    },
    [QCRYPTO_HASH_ALGO_SHA1] = {
        .init = (qcrypto_nettle_init)sha1_init,
        .write = (qcrypto_nettle_write)sha1_update,
        .result = (qcrypto_nettle_result)sha1_digest,
        .len = SHA1_DIGEST_SIZE,
    },
    [QCRYPTO_HASH_ALGO_SHA224] = {
        .init = (qcrypto_nettle_init)sha224_init,
        .write = (qcrypto_nettle_write)sha224_update,
        .result = (qcrypto_nettle_result)sha224_digest,
        .len = SHA224_DIGEST_SIZE,
    },
    [QCRYPTO_HASH_ALGO_SHA256] = {
        .init = (qcrypto_nettle_init)sha256_init,
        .write = (qcrypto_nettle_write)sha256_update,
        .result = (qcrypto_nettle_result)sha256_digest,
        .len = SHA256_DIGEST_SIZE,
    },
    [QCRYPTO_HASH_ALGO_SHA384] = {
        .init = (qcrypto_nettle_init)sha384_init,
        .write = (qcrypto_nettle_write)sha384_update,
        .result = (qcrypto_nettle_result)sha384_digest,
        .len = SHA384_DIGEST_SIZE,
    },
    [QCRYPTO_HASH_ALGO_SHA512] = {
        .init = (qcrypto_nettle_init)sha512_init,
        .write = (qcrypto_nettle_write)sha512_update,
        .result = (qcrypto_nettle_result)sha512_digest,
        .len = SHA512_DIGEST_SIZE,
    },
    [QCRYPTO_HASH_ALGO_RIPEMD160] = {
        .init = (qcrypto_nettle_init)ripemd160_init,
        .write = (qcrypto_nettle_write)ripemd160_update,
        .result = (qcrypto_nettle_result)ripemd160_digest,
        .len = RIPEMD160_DIGEST_SIZE,
    },
#ifdef CONFIG_CRYPTO_SM3
    [QCRYPTO_HASH_ALGO_SM3] = {
        .init = (qcrypto_nettle_init)sm3_init,
        .write = (qcrypto_nettle_write)sm3_update,
        .result = (qcrypto_nettle_result)sm3_digest,
        .len = SM3_DIGEST_SIZE,
    },
#endif
};

gboolean qcrypto_hash_supports(QCryptoHashAlgo alg)
{
    if (alg < G_N_ELEMENTS(qcrypto_hash_alg_map) &&
        qcrypto_hash_alg_map[alg].init != NULL) {
        return true;
    }
    return false;
}

static
QCryptoHash *qcrypto_nettle_hash_new(QCryptoHashAlgo alg, Error **errp)
{
    QCryptoHash *hash;

    hash = g_new(QCryptoHash, 1);
    hash->alg = alg;
    hash->opaque = g_new(union qcrypto_hash_ctx, 1);

    qcrypto_hash_alg_map[alg].init(hash->opaque);
    return hash;
}

static
void qcrypto_nettle_hash_free(QCryptoHash *hash)
{
    union qcrypto_hash_ctx *ctx = hash->opaque;

    g_free(ctx);
    g_free(hash);
}

static
int qcrypto_nettle_hash_update(QCryptoHash *hash,
                               const struct iovec *iov,
                               size_t niov,
                               Error **errp)
{
    union qcrypto_hash_ctx *ctx = hash->opaque;

    for (int i = 0; i < niov; i++) {
        qcrypto_hash_alg_map[hash->alg].write(ctx,
                                              iov[i].iov_len,
                                              iov[i].iov_base);
    }

    return 0;
}

static
int qcrypto_nettle_hash_finalize(QCryptoHash *hash,
                                 uint8_t **result,
                                 size_t *result_len,
                                 Error **errp)
{
    union qcrypto_hash_ctx *ctx = hash->opaque;
    int ret = qcrypto_hash_alg_map[hash->alg].len;

    if (*result_len == 0) {
        *result_len = ret;
        *result = g_new(uint8_t, *result_len);
    } else if (*result_len != ret) {
        error_setg(errp,
                   "Result buffer size %zu is smaller than hash %d",
                   *result_len, ret);
        return -1;
    }

    qcrypto_hash_alg_map[hash->alg].result(ctx, *result_len, *result);

    return 0;
}

QCryptoHashDriver qcrypto_hash_lib_driver = {
    .hash_new      = qcrypto_nettle_hash_new,
    .hash_update   = qcrypto_nettle_hash_update,
    .hash_finalize = qcrypto_nettle_hash_finalize,
    .hash_free     = qcrypto_nettle_hash_free,
};
