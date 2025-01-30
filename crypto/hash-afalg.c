/*
 * QEMU Crypto af_alg-backend hash/hmac support
 *
 * Copyright (c) 2024 Seagate Technology LLC and/or its Affiliates
 * Copyright (c) 2017 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Longpeng(Mike) <longpeng2@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "qemu/sockets.h"
#include "qapi/error.h"
#include "crypto/hash.h"
#include "crypto/hmac.h"
#include "hashpriv.h"
#include "hmacpriv.h"

static char *
qcrypto_afalg_hash_format_name(QCryptoHashAlgo alg,
                               bool is_hmac,
                               Error **errp)
{
    char *name;
    const char *alg_name;

    switch (alg) {
    case QCRYPTO_HASH_ALGO_MD5:
        alg_name = "md5";
        break;
    case QCRYPTO_HASH_ALGO_SHA1:
        alg_name = "sha1";
        break;
    case QCRYPTO_HASH_ALGO_SHA224:
        alg_name = "sha224";
        break;
    case QCRYPTO_HASH_ALGO_SHA256:
        alg_name = "sha256";
        break;
    case QCRYPTO_HASH_ALGO_SHA384:
        alg_name = "sha384";
        break;
    case QCRYPTO_HASH_ALGO_SHA512:
        alg_name = "sha512";
        break;
    case QCRYPTO_HASH_ALGO_RIPEMD160:
        alg_name = "rmd160";
        break;

    default:
        error_setg(errp, "Unsupported hash algorithm %d", alg);
        return NULL;
    }

    if (is_hmac) {
        name = g_strdup_printf("hmac(%s)", alg_name);
    } else {
        name = g_strdup(alg_name);
    }

    return name;
}

static QCryptoAFAlgo *
qcrypto_afalg_hash_hmac_ctx_new(QCryptoHashAlgo alg,
                                const uint8_t *key, size_t nkey,
                                bool is_hmac, Error **errp)
{
    QCryptoAFAlgo *afalg;
    char *name;

    name = qcrypto_afalg_hash_format_name(alg, is_hmac, errp);
    if (!name) {
        return NULL;
    }

    afalg = qcrypto_afalg_comm_alloc(AFALG_TYPE_HASH, name, errp);
    if (!afalg) {
        g_free(name);
        return NULL;
    }

    g_free(name);

    /* HMAC needs setkey */
    if (is_hmac) {
        if (setsockopt(afalg->tfmfd, SOL_ALG, ALG_SET_KEY,
                       key, nkey) != 0) {
            error_setg_errno(errp, errno, "Set hmac key failed");
            qcrypto_afalg_comm_free(afalg);
            return NULL;
        }
    }

    return afalg;
}

static QCryptoAFAlgo *
qcrypto_afalg_hash_ctx_new(QCryptoHashAlgo alg,
                           Error **errp)
{
    return qcrypto_afalg_hash_hmac_ctx_new(alg, NULL, 0, false, errp);
}

QCryptoAFAlgo *
qcrypto_afalg_hmac_ctx_new(QCryptoHashAlgo alg,
                           const uint8_t *key, size_t nkey,
                           Error **errp)
{
    return qcrypto_afalg_hash_hmac_ctx_new(alg, key, nkey, true, errp);
}

static
QCryptoHash *qcrypto_afalg_hash_new(QCryptoHashAlgo alg, Error **errp)
{
    /* Check if hash algorithm is supported */
    char *alg_name = qcrypto_afalg_hash_format_name(alg, false, NULL);
    QCryptoHash *hash;

    if (alg_name == NULL) {
        error_setg(errp, "Unknown hash algorithm %d", alg);
        return NULL;
    }

    g_free(alg_name);

    hash = g_new(QCryptoHash, 1);
    hash->alg = alg;
    hash->opaque = qcrypto_afalg_hash_ctx_new(alg, errp);
    if (!hash->opaque) {
        free(hash);
        return NULL;
    }

    return hash;
}

static
void qcrypto_afalg_hash_free(QCryptoHash *hash)
{
    QCryptoAFAlgo *ctx = hash->opaque;

    if (ctx) {
        qcrypto_afalg_comm_free(ctx);
    }

    g_free(hash);
}

/**
 * Send data to the kernel's crypto core.
 *
 * The more_data parameter is used to notify the crypto engine
 * that this is an "update" operation, and that more data will
 * be provided to calculate the final hash.
 */
static
int qcrypto_afalg_send_to_kernel(QCryptoAFAlgo *afalg,
                                 const struct iovec *iov,
                                 size_t niov,
                                 bool more_data,
                                 Error **errp)
{
    int ret = 0;
    int flags = (more_data ? MSG_MORE : 0);

    /* send data to kernel's crypto core */
    ret = iov_send_recv_with_flags(afalg->opfd, flags, iov, niov,
                                   0, iov_size(iov, niov), true);
    if (ret < 0) {
        error_setg_errno(errp, errno, "Send data to afalg-core failed");
        ret = -1;
    } else {
        /* No error, so return 0 */
        ret = 0;
    }

    return ret;
}

static
int qcrypto_afalg_recv_from_kernel(QCryptoAFAlgo *afalg,
                                   QCryptoHashAlgo alg,
                                   uint8_t **result,
                                   size_t *result_len,
                                   Error **errp)
{
    struct iovec outv;
    int ret;
    const int expected_len = qcrypto_hash_digest_len(alg);

    if (*result_len == 0) {
        *result_len = expected_len;
        *result = g_new0(uint8_t, *result_len);
    } else if (*result_len != expected_len) {
        error_setg(errp,
                   "Result buffer size %zu is not match hash %d",
                   *result_len, expected_len);
        return -1;
    }

    /* hash && get result */
    outv.iov_base = *result;
    outv.iov_len = *result_len;
    ret = iov_send_recv(afalg->opfd, &outv, 1,
                        0, iov_size(&outv, 1), false);
    if (ret < 0) {
        error_setg_errno(errp, errno, "Recv result from afalg-core failed");
        return -1;
    }

    return 0;
}

static
int qcrypto_afalg_hash_update(QCryptoHash *hash,
                              const struct iovec *iov,
                              size_t niov,
                              Error **errp)
{
    return qcrypto_afalg_send_to_kernel((QCryptoAFAlgo *) hash->opaque,
                                        iov, niov, true, errp);
}

static
int qcrypto_afalg_hash_finalize(QCryptoHash *hash,
                                 uint8_t **result,
                                 size_t *result_len,
                                 Error **errp)
{
    return qcrypto_afalg_recv_from_kernel((QCryptoAFAlgo *) hash->opaque,
                                          hash->alg, result, result_len, errp);
}

static int
qcrypto_afalg_hash_hmac_bytesv(QCryptoAFAlgo *hmac,
                               QCryptoHashAlgo alg,
                               const struct iovec *iov,
                               size_t niov, uint8_t **result,
                               size_t *resultlen,
                               Error **errp)
{
    int ret = 0;

    ret = qcrypto_afalg_send_to_kernel(hmac, iov, niov, false, errp);
    if (ret == 0) {
        ret = qcrypto_afalg_recv_from_kernel(hmac, alg, result,
                                             resultlen, errp);
    }

    return ret;
}

static int
qcrypto_afalg_hmac_bytesv(QCryptoHmac *hmac,
                          const struct iovec *iov,
                          size_t niov, uint8_t **result,
                          size_t *resultlen,
                          Error **errp)
{
    return qcrypto_afalg_hash_hmac_bytesv(hmac->opaque, hmac->alg,
                                          iov, niov, result, resultlen,
                                          errp);
}

static void qcrypto_afalg_hmac_ctx_free(QCryptoHmac *hmac)
{
    QCryptoAFAlgo *afalg;

    afalg = hmac->opaque;
    qcrypto_afalg_comm_free(afalg);
}

QCryptoHashDriver qcrypto_hash_afalg_driver = {
    .hash_new      = qcrypto_afalg_hash_new,
    .hash_free     = qcrypto_afalg_hash_free,
    .hash_update   = qcrypto_afalg_hash_update,
    .hash_finalize = qcrypto_afalg_hash_finalize
};

QCryptoHmacDriver qcrypto_hmac_afalg_driver = {
    .hmac_bytesv = qcrypto_afalg_hmac_bytesv,
    .hmac_free = qcrypto_afalg_hmac_ctx_free,
};
