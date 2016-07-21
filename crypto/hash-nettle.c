/*
 * QEMU Crypto hash algorithms
 *
 * Copyright (c) 2016 Red Hat, Inc.
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
#include <nettle/md5.h>
#include <nettle/sha.h>
#include <nettle/ripemd160.h>

typedef void (*qcrypto_nettle_init)(void *ctx);
typedef void (*qcrypto_nettle_write)(void *ctx,
                                     unsigned int len,
                                     const uint8_t *buf);
typedef void (*qcrypto_nettle_result)(void *ctx,
                                      unsigned int len,
                                      uint8_t *buf);

union qcrypto_hash_ctx {
    struct md5_ctx md5;
    struct sha1_ctx sha1;
    struct sha224_ctx sha224;
    struct sha256_ctx sha256;
    struct sha384_ctx sha384;
    struct sha512_ctx sha512;
    struct ripemd160_ctx ripemd160;
};

struct qcrypto_hash_alg {
    qcrypto_nettle_init init;
    qcrypto_nettle_write write;
    qcrypto_nettle_result result;
    size_t len;
} qcrypto_hash_alg_map[] = {
    [QCRYPTO_HASH_ALG_MD5] = {
        .init = (qcrypto_nettle_init)md5_init,
        .write = (qcrypto_nettle_write)md5_update,
        .result = (qcrypto_nettle_result)md5_digest,
        .len = MD5_DIGEST_SIZE,
    },
    [QCRYPTO_HASH_ALG_SHA1] = {
        .init = (qcrypto_nettle_init)sha1_init,
        .write = (qcrypto_nettle_write)sha1_update,
        .result = (qcrypto_nettle_result)sha1_digest,
        .len = SHA1_DIGEST_SIZE,
    },
    [QCRYPTO_HASH_ALG_SHA224] = {
        .init = (qcrypto_nettle_init)sha224_init,
        .write = (qcrypto_nettle_write)sha224_update,
        .result = (qcrypto_nettle_result)sha224_digest,
        .len = SHA224_DIGEST_SIZE,
    },
    [QCRYPTO_HASH_ALG_SHA256] = {
        .init = (qcrypto_nettle_init)sha256_init,
        .write = (qcrypto_nettle_write)sha256_update,
        .result = (qcrypto_nettle_result)sha256_digest,
        .len = SHA256_DIGEST_SIZE,
    },
    [QCRYPTO_HASH_ALG_SHA384] = {
        .init = (qcrypto_nettle_init)sha384_init,
        .write = (qcrypto_nettle_write)sha384_update,
        .result = (qcrypto_nettle_result)sha384_digest,
        .len = SHA384_DIGEST_SIZE,
    },
    [QCRYPTO_HASH_ALG_SHA512] = {
        .init = (qcrypto_nettle_init)sha512_init,
        .write = (qcrypto_nettle_write)sha512_update,
        .result = (qcrypto_nettle_result)sha512_digest,
        .len = SHA512_DIGEST_SIZE,
    },
    [QCRYPTO_HASH_ALG_RIPEMD160] = {
        .init = (qcrypto_nettle_init)ripemd160_init,
        .write = (qcrypto_nettle_write)ripemd160_update,
        .result = (qcrypto_nettle_result)ripemd160_digest,
        .len = RIPEMD160_DIGEST_SIZE,
    },
};

gboolean qcrypto_hash_supports(QCryptoHashAlgorithm alg)
{
    if (alg < G_N_ELEMENTS(qcrypto_hash_alg_map) &&
        qcrypto_hash_alg_map[alg].init != NULL) {
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
    int i;
    union qcrypto_hash_ctx ctx;

    if (!qcrypto_hash_supports(alg)) {
        error_setg(errp,
                   "Unknown hash algorithm %d",
                   alg);
        return -1;
    }

    qcrypto_hash_alg_map[alg].init(&ctx);

    for (i = 0; i < niov; i++) {
        /* Some versions of nettle have functions
         * declared with 'int' instead of 'size_t'
         * so to be safe avoid writing more than
         * UINT_MAX bytes at a time
         */
        size_t len = iov[i].iov_len;
        uint8_t *base = iov[i].iov_base;
        while (len) {
            size_t shortlen = MIN(len, UINT_MAX);
            qcrypto_hash_alg_map[alg].write(&ctx, len, base);
            len -= shortlen;
            base += len;
        }
    }

    if (*resultlen == 0) {
        *resultlen = qcrypto_hash_alg_map[alg].len;
        *result = g_new0(uint8_t, *resultlen);
    } else if (*resultlen != qcrypto_hash_alg_map[alg].len) {
        error_setg(errp,
                   "Result buffer size %zu is smaller than hash %zu",
                   *resultlen, qcrypto_hash_alg_map[alg].len);
        return -1;
    }

    qcrypto_hash_alg_map[alg].result(&ctx, *resultlen, *result);

    return 0;
}
