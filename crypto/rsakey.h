/*
 * QEMU Crypto RSA key parser
 *
 * Copyright (c) 2022 Bytedance
 * Author: lei he <helei.sig11@bytedance.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef QCRYPTO_RSAKEY_H
#define QCRYPTO_RSAKEY_H

#include "qemu/host-utils.h"
#include "crypto/akcipher.h"

typedef struct QCryptoAkCipherRSAKey QCryptoAkCipherRSAKey;
typedef struct QCryptoAkCipherMPI QCryptoAkCipherMPI;

/**
 * Multiple precious integer, encoded as two' complement,
 * copied directly from DER encoded ASN.1 structures.
 */
struct QCryptoAkCipherMPI {
    uint8_t *data;
    size_t len;
};

/* See rfc2437: https://datatracker.ietf.org/doc/html/rfc2437 */
struct QCryptoAkCipherRSAKey {
    /* The modulus */
    QCryptoAkCipherMPI n;
    /* The public exponent */
    QCryptoAkCipherMPI e;
    /* The private exponent */
    QCryptoAkCipherMPI d;
    /* The first factor */
    QCryptoAkCipherMPI p;
    /* The second factor */
    QCryptoAkCipherMPI q;
    /* The first factor's exponent */
    QCryptoAkCipherMPI dp;
    /* The second factor's exponent */
    QCryptoAkCipherMPI dq;
    /* The CRT coefficient */
    QCryptoAkCipherMPI u;
};

/**
 * Parse DER encoded ASN.1 RSA keys, expected ASN.1 schemas:
 *        RsaPrivKey ::= SEQUENCE {
 *             version     INTEGER
 *             n           INTEGER
 *             e           INTEGER
 *             d           INTEGER
 *             p           INTEGER
 *             q           INTEGER
 *             dp          INTEGER
 *             dq          INTEGER
 *             u           INTEGER
 *       otherPrimeInfos   OtherPrimeInfos OPTIONAL
 *         }
 *
 *        RsaPubKey ::= SEQUENCE {
 *             n           INTEGER
 *             e           INTEGER
 *         }
 *
 * Returns: On success QCryptoAkCipherRSAKey is returned, otherwise returns NULL
 */
QCryptoAkCipherRSAKey *qcrypto_akcipher_rsakey_parse(
    QCryptoAkCipherKeyType type,
    const uint8_t *key, size_t keylen, Error **errp);

/**
 * qcrypto_akcipher_rsakey_export_as_p8info:
 *
 * Export RSA private key to PKCS#8 private key info.
 */
void qcrypto_akcipher_rsakey_export_p8info(const uint8_t *key,
                                           size_t keylen,
                                           uint8_t **dst,
                                           size_t *dlen);

void qcrypto_akcipher_rsakey_free(QCryptoAkCipherRSAKey *key);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(QCryptoAkCipherRSAKey,
                              qcrypto_akcipher_rsakey_free);

#endif
