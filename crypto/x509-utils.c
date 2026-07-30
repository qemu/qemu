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

#ifdef CONFIG_GNUTLS
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

static const int qcrypto_to_gnutls_keyid_flags_map[] = {
    [QCRYPTO_HASH_ALGO_MD5] = -1,
    [QCRYPTO_HASH_ALGO_SHA1] = GNUTLS_KEYID_USE_SHA1,
    [QCRYPTO_HASH_ALGO_SHA224] = -1,
    [QCRYPTO_HASH_ALGO_SHA256] = GNUTLS_KEYID_USE_SHA256,
    [QCRYPTO_HASH_ALGO_SHA384] = -1,
    [QCRYPTO_HASH_ALGO_SHA512] = GNUTLS_KEYID_USE_SHA512,
    [QCRYPTO_HASH_ALGO_RIPEMD160] = -1,
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

    ret = gnutls_x509_crt_init(&crt);
    if (ret < 0) {
        error_setg(errp, "Unable to initialize certificate: %s",
                   gnutls_strerror(ret));
        return -1;
    }

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

int qcrypto_x509_convert_cert_der(uint8_t *cert, size_t size,
                                  uint8_t **result, size_t *resultlen,
                                  Error **errp)
{
    int ret = -1;
    int rc;
    gnutls_x509_crt_t crt;
    gnutls_datum_t datum = {.data = cert, .size = size};
    gnutls_datum_t datum_der = {.data = NULL, .size = 0};

    rc = gnutls_x509_crt_init(&crt);
    if (rc < 0) {
        error_setg(errp, "Failed to initialize certificate: %s", gnutls_strerror(rc));
        return ret;
    }

    rc = gnutls_x509_crt_import(crt, &datum, GNUTLS_X509_FMT_PEM);
    if (rc != 0) {
        error_setg(errp, "Failed to import certificate: %s", gnutls_strerror(rc));
        goto cleanup;
    }

    rc = gnutls_x509_crt_export2(crt, GNUTLS_X509_FMT_DER, &datum_der);
    if (rc != 0) {
        error_setg(errp, "Failed to convert certificate to DER format: %s",
                   gnutls_strerror(rc));
        goto cleanup;
    }

    *resultlen = datum_der.size;
    *result = g_memdup2(datum_der.data, datum_der.size);

    ret = 0;

cleanup:
    gnutls_x509_crt_deinit(crt);
    g_free(datum_der.data);
    return ret;
}

int qcrypto_x509_check_cert_times(uint8_t *cert, size_t size, Error **errp)
{
    int rc;
    int ret = -1;
    gnutls_x509_crt_t crt;
    gnutls_datum_t datum = {.data = cert, .size = size};
    time_t now = time(NULL);
    time_t exp_time;
    time_t act_time;

    if (now == ((time_t)-1)) {
        error_setg_errno(errp, errno, "Cannot get current time");
        return ret;
    }

    rc = gnutls_x509_crt_init(&crt);
    if (rc < 0) {
        error_setg(errp, "Failed to initialize certificate: %s", gnutls_strerror(rc));
        return ret;
    }

    rc = gnutls_x509_crt_import(crt, &datum, GNUTLS_X509_FMT_PEM);
    if (rc != 0) {
        error_setg(errp, "Failed to import certificate: %s", gnutls_strerror(rc));
        goto cleanup;
    }

    exp_time = gnutls_x509_crt_get_expiration_time(crt);
    if (exp_time == ((time_t)-1)) {
        error_setg(errp, "Failed to get certificate expiration time");
        goto cleanup;
    }
    if (exp_time < now) {
        error_setg(errp, "The certificate has expired");
        goto cleanup;
    }

    act_time = gnutls_x509_crt_get_activation_time(crt);
    if (act_time == ((time_t)-1)) {
        error_setg(errp, "Failed to get certificate activation time");
        goto cleanup;
    }
    if (act_time > now) {
        error_setg(errp, "The certificate is not yet active");
        goto cleanup;
    }

    ret = 0;

cleanup:
    gnutls_x509_crt_deinit(crt);
    return ret;
}

static int qcrypto_x509_get_pk_algorithm(uint8_t *cert, size_t size, Error **errp)
{
    int rc;
    int ret = -1;
    unsigned int bits;
    gnutls_x509_crt_t crt;
    gnutls_datum_t datum = {.data = cert, .size = size};

    rc = gnutls_x509_crt_init(&crt);
    if (rc < 0) {
        error_setg(errp, "Failed to initialize certificate: %s", gnutls_strerror(rc));
        return ret;
    }

    rc = gnutls_x509_crt_import(crt, &datum, GNUTLS_X509_FMT_PEM);
    if (rc != 0) {
        error_setg(errp, "Failed to import certificate: %s", gnutls_strerror(rc));
        goto cleanup;
    }

    rc = gnutls_x509_crt_get_pk_algorithm(crt, &bits);
    if (rc < 0) {
        error_setg(errp, "Unknown public key algorithm %d", rc);
        goto cleanup;
    }

    ret = rc;

cleanup:
    gnutls_x509_crt_deinit(crt);
    return ret;
}

int qcrypto_x509_get_cert_key_id(uint8_t *cert, size_t size,
                                 QCryptoHashAlgo hash_alg,
                                 uint8_t **result,
                                 size_t *resultlen,
                                 Error **errp)
{
    int rc;
    int ret = -1;
    gnutls_x509_crt_t crt;
    gnutls_datum_t datum = {.data = cert, .size = size};

    if (hash_alg >= G_N_ELEMENTS(qcrypto_to_gnutls_hash_alg_map)) {
        error_setg(errp, "Unknown hash algorithm %d", hash_alg);
        return ret;
    }

    if (hash_alg >= G_N_ELEMENTS(qcrypto_to_gnutls_keyid_flags_map) ||
        qcrypto_to_gnutls_keyid_flags_map[hash_alg] == -1) {
        error_setg(errp, "Unsupported key id flag %d", hash_alg);
        return ret;
    }

    rc = gnutls_x509_crt_init(&crt);
    if (rc < 0) {
        error_setg(errp, "Failed to initialize certificate: %s", gnutls_strerror(rc));
        return ret;
    }

    rc = gnutls_x509_crt_import(crt, &datum, GNUTLS_X509_FMT_PEM);
    if (rc != 0) {
        error_setg(errp, "Failed to import certificate: %s", gnutls_strerror(rc));
        goto cleanup;
    }

    *resultlen = gnutls_hash_get_len(qcrypto_to_gnutls_hash_alg_map[hash_alg]);
    if (*resultlen == 0) {
        error_setg(errp, "Failed to get hash algorithm length: %s", gnutls_strerror(rc));
        goto cleanup;
    }

    *result = g_malloc0(*resultlen);
    if (gnutls_x509_crt_get_key_id(crt,
                                   qcrypto_to_gnutls_keyid_flags_map[hash_alg],
                                   *result, resultlen) != 0) {
        error_setg(errp, "Failed to get key ID from certificate");
        g_clear_pointer(result, g_free);
        goto cleanup;
    }

    ret = 0;

cleanup:
    gnutls_x509_crt_deinit(crt);
    return ret;
}

static int qcrypto_x509_get_ecc_curve(uint8_t *cert, size_t size, Error **errp)
{
    int rc;
    int ret = -1;
    gnutls_x509_crt_t crt;
    gnutls_datum_t datum = {.data = cert, .size = size};
    gnutls_ecc_curve_t curve_id;
    gnutls_datum_t x = {.data = NULL, .size = 0};
    gnutls_datum_t y = {.data = NULL, .size = 0};

    rc = gnutls_x509_crt_init(&crt);
    if (rc < 0) {
        error_setg(errp, "Failed to initialize certificate: %s", gnutls_strerror(rc));
        return ret;
    }

    rc = gnutls_x509_crt_import(crt, &datum, GNUTLS_X509_FMT_PEM);
    if (rc != 0) {
        error_setg(errp, "Failed to import certificate: %s", gnutls_strerror(rc));
        goto cleanup;
    }

    rc = gnutls_x509_crt_get_pk_ecc_raw(crt, &curve_id, &x, &y);
    if (rc != 0) {
        error_setg(errp, "Failed to get ECC public key curve: %s", gnutls_strerror(rc));
        goto cleanup;
    }

    ret = curve_id;

cleanup:
    gnutls_x509_crt_deinit(crt);
    g_free(x.data);
    g_free(y.data);
    return ret;
}

int qcrypto_x509_check_ecc_curve_p521(uint8_t *cert, size_t size, Error **errp)
{
    int algo;
    int curve_id;

    algo = qcrypto_x509_get_pk_algorithm(cert, size, errp);
    if (algo != GNUTLS_PK_ECDSA) {
        return 0;
    }

    curve_id = qcrypto_x509_get_ecc_curve(cert, size, errp);
    if (curve_id == -1) {
        error_setg(errp, "Failed to get ECC curve");
        return -1;
    }

    if (curve_id == GNUTLS_ECC_CURVE_INVALID) {
        error_setg(errp, "Invalid ECC curve");
        return -1;
    }

    return curve_id == GNUTLS_ECC_CURVE_SECP521R1;
}

#else /* ! CONFIG_GNUTLS */

int qcrypto_get_x509_cert_fingerprint(uint8_t *cert, size_t size,
                                      QCryptoHashAlgo hash,
                                      uint8_t *result,
                                      size_t *resultlen,
                                      Error **errp)
{
    error_setg(errp, "GNUTLS is required to get fingerprint");
    return -1;
}

int qcrypto_x509_convert_cert_der(uint8_t *cert, size_t size,
                                  uint8_t **result,
                                  size_t *resultlen,
                                  Error **errp)
{
    error_setg(errp, "GNUTLS is required to export X.509 certificate");
    return -1;
}

int qcrypto_x509_check_cert_times(uint8_t *cert, size_t size, Error **errp)
{
    error_setg(errp, "GNUTLS is required to get certificate times");
    return -1;
}

int qcrypto_x509_get_cert_key_id(uint8_t *cert, size_t size,
                                 QCryptoHashAlgo hash_alg,
                                 uint8_t **result,
                                 size_t *resultlen,
                                 Error **errp)
{
    error_setg(errp, "GNUTLS is required to get key ID");
    return -1;
}

int qcrypto_x509_check_ecc_curve_p521(uint8_t *cert, size_t size, Error **errp)
{
    error_setg(errp, "GNUTLS is required to determine ecc curve");
    return -1;
}

#endif /* ! CONFIG_GNUTLS */
