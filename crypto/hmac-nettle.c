/*
 * QEMU Crypto hmac algorithms (based on nettle)
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Longpeng(Mike) <longpeng2@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "crypto/hmac.h"
#include "hmacpriv.h"
#include <nettle/hmac.h>

typedef void (*qcrypto_nettle_hmac_setkey)(void *ctx,
              size_t key_length, const uint8_t *key);

typedef void (*qcrypto_nettle_hmac_update)(void *ctx,
              size_t length, const uint8_t *data);

typedef void (*qcrypto_nettle_hmac_digest)(void *ctx,
              size_t length, uint8_t *digest);

typedef struct QCryptoHmacNettle QCryptoHmacNettle;
struct QCryptoHmacNettle {
    union qcrypto_nettle_hmac_ctx {
        struct hmac_md5_ctx md5_ctx;
        struct hmac_sha1_ctx sha1_ctx;
        struct hmac_sha256_ctx sha256_ctx; /* equals hmac_sha224_ctx */
        struct hmac_sha512_ctx sha512_ctx; /* equals hmac_sha384_ctx */
        struct hmac_ripemd160_ctx ripemd160_ctx;
    } u;
};

struct qcrypto_nettle_hmac_alg {
    qcrypto_nettle_hmac_setkey setkey;
    qcrypto_nettle_hmac_update update;
    qcrypto_nettle_hmac_digest digest;
    size_t len;
} qcrypto_hmac_alg_map[QCRYPTO_HASH_ALG__MAX] = {
    [QCRYPTO_HASH_ALG_MD5] = {
        .setkey = (qcrypto_nettle_hmac_setkey)hmac_md5_set_key,
        .update = (qcrypto_nettle_hmac_update)hmac_md5_update,
        .digest = (qcrypto_nettle_hmac_digest)hmac_md5_digest,
        .len = MD5_DIGEST_SIZE,
    },
    [QCRYPTO_HASH_ALG_SHA1] = {
        .setkey = (qcrypto_nettle_hmac_setkey)hmac_sha1_set_key,
        .update = (qcrypto_nettle_hmac_update)hmac_sha1_update,
        .digest = (qcrypto_nettle_hmac_digest)hmac_sha1_digest,
        .len = SHA1_DIGEST_SIZE,
    },
    [QCRYPTO_HASH_ALG_SHA224] = {
        .setkey = (qcrypto_nettle_hmac_setkey)hmac_sha224_set_key,
        .update = (qcrypto_nettle_hmac_update)hmac_sha224_update,
        .digest = (qcrypto_nettle_hmac_digest)hmac_sha224_digest,
        .len = SHA224_DIGEST_SIZE,
    },
    [QCRYPTO_HASH_ALG_SHA256] = {
        .setkey = (qcrypto_nettle_hmac_setkey)hmac_sha256_set_key,
        .update = (qcrypto_nettle_hmac_update)hmac_sha256_update,
        .digest = (qcrypto_nettle_hmac_digest)hmac_sha256_digest,
        .len = SHA256_DIGEST_SIZE,
    },
    [QCRYPTO_HASH_ALG_SHA384] = {
        .setkey = (qcrypto_nettle_hmac_setkey)hmac_sha384_set_key,
        .update = (qcrypto_nettle_hmac_update)hmac_sha384_update,
        .digest = (qcrypto_nettle_hmac_digest)hmac_sha384_digest,
        .len = SHA384_DIGEST_SIZE,
    },
    [QCRYPTO_HASH_ALG_SHA512] = {
        .setkey = (qcrypto_nettle_hmac_setkey)hmac_sha512_set_key,
        .update = (qcrypto_nettle_hmac_update)hmac_sha512_update,
        .digest = (qcrypto_nettle_hmac_digest)hmac_sha512_digest,
        .len = SHA512_DIGEST_SIZE,
    },
    [QCRYPTO_HASH_ALG_RIPEMD160] = {
        .setkey = (qcrypto_nettle_hmac_setkey)hmac_ripemd160_set_key,
        .update = (qcrypto_nettle_hmac_update)hmac_ripemd160_update,
        .digest = (qcrypto_nettle_hmac_digest)hmac_ripemd160_digest,
        .len = RIPEMD160_DIGEST_SIZE,
    },
};

bool qcrypto_hmac_supports(QCryptoHashAlgorithm alg)
{
    if (alg < G_N_ELEMENTS(qcrypto_hmac_alg_map) &&
        qcrypto_hmac_alg_map[alg].setkey != NULL) {
        return true;
    }

    return false;
}

void *qcrypto_hmac_ctx_new(QCryptoHashAlgorithm alg,
                           const uint8_t *key, size_t nkey,
                           Error **errp)
{
    QCryptoHmacNettle *ctx;

    if (!qcrypto_hmac_supports(alg)) {
        error_setg(errp, "Unsupported hmac algorithm %s",
                   QCryptoHashAlgorithm_str(alg));
        return NULL;
    }

    ctx = g_new0(QCryptoHmacNettle, 1);

    qcrypto_hmac_alg_map[alg].setkey(&ctx->u, nkey, key);

    return ctx;
}

static void
qcrypto_nettle_hmac_ctx_free(QCryptoHmac *hmac)
{
    QCryptoHmacNettle *ctx;

    ctx = hmac->opaque;
    g_free(ctx);
}

static int
qcrypto_nettle_hmac_bytesv(QCryptoHmac *hmac,
                           const struct iovec *iov,
                           size_t niov,
                           uint8_t **result,
                           size_t *resultlen,
                           Error **errp)
{
    QCryptoHmacNettle *ctx;
    int i;

    ctx = (QCryptoHmacNettle *)hmac->opaque;

    for (i = 0; i < niov; ++i) {
        size_t len = iov[i].iov_len;
        uint8_t *base = iov[i].iov_base;
        while (len) {
            size_t shortlen = MIN(len, UINT_MAX);
            qcrypto_hmac_alg_map[hmac->alg].update(&ctx->u, len, base);
            len -= shortlen;
            base += len;
        }
    }

    if (*resultlen == 0) {
        *resultlen = qcrypto_hmac_alg_map[hmac->alg].len;
        *result = g_new0(uint8_t, *resultlen);
    } else if (*resultlen != qcrypto_hmac_alg_map[hmac->alg].len) {
        error_setg(errp,
                   "Result buffer size %zu is smaller than hash %zu",
                   *resultlen, qcrypto_hmac_alg_map[hmac->alg].len);
        return -1;
    }

    qcrypto_hmac_alg_map[hmac->alg].digest(&ctx->u, *resultlen, *result);

    return 0;
}

QCryptoHmacDriver qcrypto_hmac_lib_driver = {
    .hmac_bytesv = qcrypto_nettle_hmac_bytesv,
    .hmac_free = qcrypto_nettle_hmac_ctx_free,
};
