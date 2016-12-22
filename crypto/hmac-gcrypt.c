/*
 * QEMU Crypto hmac algorithms (based on libgcrypt)
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
#include <gcrypt.h>

static int qcrypto_hmac_alg_map[QCRYPTO_HASH_ALG__MAX] = {
    [QCRYPTO_HASH_ALG_MD5] = GCRY_MAC_HMAC_MD5,
    [QCRYPTO_HASH_ALG_SHA1] = GCRY_MAC_HMAC_SHA1,
    [QCRYPTO_HASH_ALG_SHA224] = GCRY_MAC_HMAC_SHA224,
    [QCRYPTO_HASH_ALG_SHA256] = GCRY_MAC_HMAC_SHA256,
    [QCRYPTO_HASH_ALG_SHA384] = GCRY_MAC_HMAC_SHA384,
    [QCRYPTO_HASH_ALG_SHA512] = GCRY_MAC_HMAC_SHA512,
    [QCRYPTO_HASH_ALG_RIPEMD160] = GCRY_MAC_HMAC_RMD160,
};

typedef struct QCryptoHmacGcrypt QCryptoHmacGcrypt;
struct QCryptoHmacGcrypt {
    gcry_mac_hd_t handle;
};

bool qcrypto_hmac_supports(QCryptoHashAlgorithm alg)
{
    if (alg < G_N_ELEMENTS(qcrypto_hmac_alg_map) &&
        qcrypto_hmac_alg_map[alg] != GCRY_MAC_NONE) {
        return true;
    }

    return false;
}

QCryptoHmac *qcrypto_hmac_new(QCryptoHashAlgorithm alg,
                              const uint8_t *key, size_t nkey,
                              Error **errp)
{
    QCryptoHmac *hmac;
    QCryptoHmacGcrypt *ctx;
    gcry_error_t err;

    if (!qcrypto_hmac_supports(alg)) {
        error_setg(errp, "Unsupported hmac algorithm %s",
                   QCryptoHashAlgorithm_lookup[alg]);
        return NULL;
    }

    hmac = g_new0(QCryptoHmac, 1);
    hmac->alg = alg;

    ctx = g_new0(QCryptoHmacGcrypt, 1);

    err = gcry_mac_open(&ctx->handle, qcrypto_hmac_alg_map[alg],
                        GCRY_MAC_FLAG_SECURE, NULL);
    if (err != 0) {
        error_setg(errp, "Cannot initialize hmac: %s",
                   gcry_strerror(err));
        goto error;
    }

    err = gcry_mac_setkey(ctx->handle, (const void *)key, nkey);
    if (err != 0) {
        error_setg(errp, "Cannot set key: %s",
                   gcry_strerror(err));
        goto error;
    }

    hmac->opaque = ctx;
    return hmac;

error:
    g_free(ctx);
    g_free(hmac);
    return NULL;
}

void qcrypto_hmac_free(QCryptoHmac *hmac)
{
    QCryptoHmacGcrypt *ctx;

    if (!hmac) {
        return;
    }

    ctx = hmac->opaque;
    gcry_mac_close(ctx->handle);

    g_free(ctx);
    g_free(hmac);
}

int qcrypto_hmac_bytesv(QCryptoHmac *hmac,
                        const struct iovec *iov,
                        size_t niov,
                        uint8_t **result,
                        size_t *resultlen,
                        Error **errp)
{
    QCryptoHmacGcrypt *ctx;
    gcry_error_t err;
    uint32_t ret;
    int i;

    ctx = hmac->opaque;

    for (i = 0; i < niov; i++) {
        gcry_mac_write(ctx->handle, iov[i].iov_base, iov[i].iov_len);
    }

    ret = gcry_mac_get_algo_maclen(qcrypto_hmac_alg_map[hmac->alg]);
    if (ret <= 0) {
        error_setg(errp, "Unable to get hmac length: %s",
                   gcry_strerror(ret));
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

    err = gcry_mac_read(ctx->handle, *result, resultlen);
    if (err != 0) {
        error_setg(errp, "Cannot get result: %s",
                   gcry_strerror(err));
        return -1;
    }

    err = gcry_mac_reset(ctx->handle);
    if (err != 0) {
        error_setg(errp, "Cannot reset hmac context: %s",
                   gcry_strerror(err));
        return -1;
    }

    return 0;
}
