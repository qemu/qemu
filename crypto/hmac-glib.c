/*
 * QEMU Crypto hmac algorithms (based on glib)
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

static int qcrypto_hmac_alg_map[QCRYPTO_HASH_ALG__MAX] = {
    [QCRYPTO_HASH_ALG_MD5] = G_CHECKSUM_MD5,
    [QCRYPTO_HASH_ALG_SHA1] = G_CHECKSUM_SHA1,
    [QCRYPTO_HASH_ALG_SHA256] = G_CHECKSUM_SHA256,
/* Support for HMAC SHA-512 in GLib 2.42 */
#if GLIB_CHECK_VERSION(2, 42, 0)
    [QCRYPTO_HASH_ALG_SHA512] = G_CHECKSUM_SHA512,
#else
    [QCRYPTO_HASH_ALG_SHA512] = -1,
#endif
    [QCRYPTO_HASH_ALG_SHA224] = -1,
    [QCRYPTO_HASH_ALG_SHA384] = -1,
    [QCRYPTO_HASH_ALG_RIPEMD160] = -1,
};

typedef struct QCryptoHmacGlib QCryptoHmacGlib;
struct QCryptoHmacGlib {
    GHmac *ghmac;
};

bool qcrypto_hmac_supports(QCryptoHashAlgorithm alg)
{
    if (alg < G_N_ELEMENTS(qcrypto_hmac_alg_map) &&
        qcrypto_hmac_alg_map[alg] != -1) {
        return true;
    }

    return false;
}

void *qcrypto_hmac_ctx_new(QCryptoHashAlgorithm alg,
                           const uint8_t *key, size_t nkey,
                           Error **errp)
{
    QCryptoHmacGlib *ctx;

    if (!qcrypto_hmac_supports(alg)) {
        error_setg(errp, "Unsupported hmac algorithm %s",
                   QCryptoHashAlgorithm_str(alg));
        return NULL;
    }

    ctx = g_new0(QCryptoHmacGlib, 1);

    ctx->ghmac = g_hmac_new(qcrypto_hmac_alg_map[alg],
                            (const uint8_t *)key, nkey);
    if (!ctx->ghmac) {
        error_setg(errp, "Cannot initialize hmac and set key");
        goto error;
    }

    return ctx;

error:
    g_free(ctx);
    return NULL;
}

static void
qcrypto_glib_hmac_ctx_free(QCryptoHmac *hmac)
{
    QCryptoHmacGlib *ctx;

    ctx = hmac->opaque;
    g_hmac_unref(ctx->ghmac);

    g_free(ctx);
}

static int
qcrypto_glib_hmac_bytesv(QCryptoHmac *hmac,
                         const struct iovec *iov,
                         size_t niov,
                         uint8_t **result,
                         size_t *resultlen,
                         Error **errp)
{
    QCryptoHmacGlib *ctx;
    int i, ret;

    ctx = hmac->opaque;

    for (i = 0; i < niov; i++) {
        g_hmac_update(ctx->ghmac, iov[i].iov_base, iov[i].iov_len);
    }

    ret = g_checksum_type_get_length(qcrypto_hmac_alg_map[hmac->alg]);
    if (ret < 0) {
        error_setg(errp, "Unable to get hmac length");
        return -1;
    }

    if (*resultlen == 0) {
        *resultlen = ret;
        *result = g_new0(uint8_t, *resultlen);
    } else if (*resultlen != ret) {
        error_setg(errp, "Result buffer size %zu is smaller than hmac %d",
                   *resultlen, ret);
        return -1;
    }

    g_hmac_get_digest(ctx->ghmac, *result, resultlen);

    return 0;
}

QCryptoHmacDriver qcrypto_hmac_lib_driver = {
    .hmac_bytesv = qcrypto_glib_hmac_bytesv,
    .hmac_free = qcrypto_glib_hmac_ctx_free,
};
