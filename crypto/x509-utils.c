/*
 * X.509 certificate related helpers
 *
 * Copyright (c) 2024 Dorjoy Chowdhury <dorjoychy111@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "crypto/x509-utils.h"
#include <gnutls/gnutls.h>
#include <gnutls/crypto.h>
#include <gnutls/x509.h>

static const int qcrypto_to_gnutls_hash_alg_map[QCRYPTO_HASH_ALGO__MAX] = {
    [QCRYPTO_HASH_ALGO_MD5] = GNUTLS_DIG_MD5,
    [QCRYPTO_HASH_ALGO_SHA1] = GNUTLS_DIG_SHA1,
    [QCRYPTO_HASH_ALGO_SHA224] = GNUTLS_DIG_SHA224,
    [QCRYPTO_HASH_ALGO_SHA256] = GNUTLS_DIG_SHA256,
    [QCRYPTO_HASH_ALGO_SHA384] = GNUTLS_DIG_SHA384,
    [QCRYPTO_HASH_ALGO_SHA512] = GNUTLS_DIG_SHA512,
    [QCRYPTO_HASH_ALGO_RIPEMD160] = GNUTLS_DIG_RMD160,
};

int qcrypto_get_x509_cert_fingerprint(uint8_t *cert, size_t size,
                                      QCryptoHashAlgo alg,
                                      uint8_t *result,
                                      size_t *resultlen,
                                      Error **errp)
{
    int ret = -1;
    int hlen;
    gnutls_x509_crt_t crt;
    gnutls_datum_t datum = {.data = cert, .size = size};

    if (alg >= G_N_ELEMENTS(qcrypto_to_gnutls_hash_alg_map)) {
        error_setg(errp, "Unknown hash algorithm");
        return -1;
    }

    if (result == NULL) {
        error_setg(errp, "No valid buffer given");
        return -1;
    }

    gnutls_x509_crt_init(&crt);

    if (gnutls_x509_crt_import(crt, &datum, GNUTLS_X509_FMT_PEM) != 0) {
        error_setg(errp, "Failed to import certificate");
        goto cleanup;
    }

    hlen = gnutls_hash_get_len(qcrypto_to_gnutls_hash_alg_map[alg]);
    if (*resultlen < hlen) {
        error_setg(errp,
                   "Result buffer size %zu is smaller than hash %d",
                   *resultlen, hlen);
        goto cleanup;
    }

    if (gnutls_x509_crt_get_fingerprint(crt,
                                        qcrypto_to_gnutls_hash_alg_map[alg],
                                        result, resultlen) != 0) {
        error_setg(errp, "Failed to get fingerprint from certificate");
        goto cleanup;
    }

    ret = 0;

 cleanup:
    gnutls_x509_crt_deinit(crt);
    return ret;
}
