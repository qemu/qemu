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

#endif
