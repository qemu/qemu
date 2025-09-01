/*
 * QEMU Crypto hmac algorithms
 *
 * Copyright (c) 2021 Red Hat, Inc.
 *
 * Derived from hmac-gcrypt.c:
 *
 *   Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 *
 */

#include "qemu/osdep.h"
#include <gnutls/crypto.h>

#include "qapi/error.h"
#include "crypto/hmac.h"
#include "hmacpriv.h"

static int qcrypto_hmac_alg_map[QCRYPTO_HASH_ALGO__MAX] = {
    [QCRYPTO_HASH_ALGO_MD5] = GNUTLS_MAC_MD5,
    [QCRYPTO_HASH_ALGO_SHA1] = GNUTLS_MAC_SHA1,
    [QCRYPTO_HASH_ALGO_SHA224] = GNUTLS_MAC_SHA224,
    [QCRYPTO_HASH_ALGO_SHA256] = GNUTLS_MAC_SHA256,
    [QCRYPTO_HASH_ALGO_SHA384] = GNUTLS_MAC_SHA384,
    [QCRYPTO_HASH_ALGO_SHA512] = GNUTLS_MAC_SHA512,
    [QCRYPTO_HASH_ALGO_RIPEMD160] = GNUTLS_MAC_RMD160,
};

typedef struct QCryptoHmacGnutls QCryptoHmacGnutls;
struct QCryptoHmacGnutls {
    gnutls_hmac_hd_t handle;
};

bool qcrypto_hmac_supports(QCryptoHashAlgo alg)
{
    size_t i;
    const gnutls_digest_algorithm_t *algs;
    if (alg >= G_N_ELEMENTS(qcrypto_hmac_alg_map) ||
        qcrypto_hmac_alg_map[alg] == GNUTLS_DIG_UNKNOWN) {
        return false;
    }
    algs = gnutls_digest_list();
    for (i = 0; algs[i] != GNUTLS_DIG_UNKNOWN; i++) {
        if (algs[i] == qcrypto_hmac_alg_map[alg]) {
            return true;
        }
    }
    return false;
}

void *qcrypto_hmac_ctx_new(QCryptoHashAlgo alg,
                           const uint8_t *key, size_t nkey,
                           Error **errp)
{
    QCryptoHmacGnutls *ctx;
    int err;

    if (!qcrypto_hmac_supports(alg)) {
        error_setg(errp, "Unsupported hmac algorithm %s",
                   QCryptoHashAlgo_str(alg));
        return NULL;
    }

    ctx = g_new0(QCryptoHmacGnutls, 1);

    err = gnutls_hmac_init(&ctx->handle,
                           qcrypto_hmac_alg_map[alg],
                           (const void *)key, nkey);
    if (err != 0) {
        error_setg(errp, "Cannot initialize hmac: %s",
                   gnutls_strerror(err));
        goto error;
    }

    return ctx;

error:
    g_free(ctx);
    return NULL;
}

static void
qcrypto_gnutls_hmac_ctx_free(QCryptoHmac *hmac)
{
    QCryptoHmacGnutls *ctx;

    ctx = hmac->opaque;
    gnutls_hmac_deinit(ctx->handle, NULL);

    g_free(ctx);
}

static int
qcrypto_gnutls_hmac_bytesv(QCryptoHmac *hmac,
                           const struct iovec *iov,
                           size_t niov,
                           uint8_t **result,
                           size_t *resultlen,
                           Error **errp)
{
    QCryptoHmacGnutls *ctx;
    uint32_t ret;
    int i;

    ctx = hmac->opaque;

    for (i = 0; i < niov; i++) {
        gnutls_hmac(ctx->handle, iov[i].iov_base, iov[i].iov_len);
    }

    ret = gnutls_hmac_get_len(qcrypto_hmac_alg_map[hmac->alg]);
    if (ret <= 0) {
        error_setg(errp, "Unable to get hmac length: %s",
                   gnutls_strerror(ret));
        return -1;
    }

    if (resultlen == NULL) {
        return 0;
    } else if (*resultlen == 0) {
        *resultlen = ret;
        *result = g_new0(uint8_t, *resultlen);
    } else if (*resultlen != ret) {
        error_setg(errp, "Result buffer size %zu is smaller than hmac %d",
                   *resultlen, ret);
        return -1;
    }

    gnutls_hmac_output(ctx->handle, *result);

    return 0;
}

QCryptoHmacDriver qcrypto_hmac_lib_driver = {
    .hmac_bytesv = qcrypto_gnutls_hmac_bytesv,
    .hmac_free = qcrypto_gnutls_hmac_ctx_free,
};
