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

#endif
