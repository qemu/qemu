/*
 * X.509 certificate related helpers
 *
 * Copyright (c) 2024 Dorjoy Chowdhury <dorjoychy111@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef QCRYPTO_X509_UTILS_H
#define QCRYPTO_X509_UTILS_H

#include "crypto/hash.h"

int qcrypto_get_x509_cert_fingerprint(uint8_t *cert, size_t size,
                                      QCryptoHashAlgo hash,
                                      uint8_t *result,
                                      size_t *resultlen,
                                      Error **errp);

/**
 * qcrypto_x509_convert_cert_der
 * @cert: pointer to the raw certificate data in PEM format
 * @size: size of the certificate
 * @result: output location for the allocated buffer for the certificate
 *          in DER format
 *          (the function allocates memory which must be freed by the caller)
 * @resultlen: pointer to the size of the buffer (will be updated with the
 *             actual size of the DER-encoded certificate)
 * @errp: error pointer
 *
 * Convert the given @cert from PEM to DER format.
 *
 * Returns: 0 on success,
 *         -1 on error.
 */
int qcrypto_x509_convert_cert_der(uint8_t *cert, size_t size,
                                  uint8_t **result,
                                  size_t *resultlen,
                                  Error **errp);

/**
 * qcrypto_x509_check_cert_times
 * @cert: pointer to the raw certificate data
 * @size: size of the certificate
 * @errp: error pointer
 *
 * Check whether the activation and expiration times of @cert
 * are valid at the current time.
 *
 * Returns: 0 if the certificate times are valid,
 *         -1 on error.
 */
int qcrypto_x509_check_cert_times(uint8_t *cert, size_t size, Error **errp);

/**
 * qcrypto_x509_get_cert_key_id
 * @cert: pointer to the raw certificate data
 * @size: size of the certificate
 * @hash_alg: the hash algorithm flag
 * @result: output location for the allocated buffer for key ID
 *          (the function allocates memory which must be freed by the caller)
 * @resultlen: pointer to the size of the buffer
 *             (will be updated with the actual size of key id)
 * @errp: error pointer
 *
 * Retrieve the key ID from the @cert based on the specified @hash_alg.
 *
 * Returns: 0 if key ID was successfully stored in @result,
 *         -1 on error.
 */
int qcrypto_x509_get_cert_key_id(uint8_t *cert, size_t size,
                                 QCryptoHashAlgo hash_alg,
                                 uint8_t **result,
                                 size_t *resultlen,
                                 Error **errp);

/**
 * qcrypto_x509_check_ecc_curve_p521
 * @cert: pointer to the raw certificate data
 * @size: size of the certificate
 * @errp: error pointer
 *
 * Determine whether the ECC public key in the given certificate uses the P-521
 * curve.
 *
 * Returns: 0 if ECC public key does not use P521 curve.
 *          1 if ECC public key uses P521 curve.
 *         -1 on error.
 */
int qcrypto_x509_check_ecc_curve_p521(uint8_t *cert, size_t size, Error **errp);

#endif
