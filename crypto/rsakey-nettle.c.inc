/*
 * QEMU Crypto akcipher algorithms
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

#include <nettle/asn1.h>

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "rsakey.h"

static bool DumpMPI(struct asn1_der_iterator *i, QCryptoAkCipherMPI *mpi)
{
    mpi->data = g_memdup2(i->data, i->length);
    mpi->len = i->length;
    return true;
}

static bool GetMPI(struct asn1_der_iterator *i, QCryptoAkCipherMPI *mpi)
{
    if (asn1_der_iterator_next(i) != ASN1_ITERATOR_PRIMITIVE ||
        i->type != ASN1_INTEGER) {
        return false;
    }
    return DumpMPI(i, mpi);
}

/**
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
 */
static QCryptoAkCipherRSAKey *qcrypto_nettle_rsa_private_key_parse(
    const uint8_t *key, size_t keylen, Error **errp)
{
    QCryptoAkCipherRSAKey *rsa = g_new0(QCryptoAkCipherRSAKey, 1);
    struct asn1_der_iterator i;
    uint32_t version;
    int tag;

    /* Parse entire struct */
    if (asn1_der_iterator_first(&i, keylen, key) != ASN1_ITERATOR_CONSTRUCTED ||
        i.type != ASN1_SEQUENCE ||
        asn1_der_decode_constructed_last(&i) != ASN1_ITERATOR_PRIMITIVE ||
        i.type != ASN1_INTEGER ||
        !asn1_der_get_uint32(&i, &version) ||
        version > 1 ||
        !GetMPI(&i, &rsa->n) ||
        !GetMPI(&i, &rsa->e) ||
        !GetMPI(&i, &rsa->d) ||
        !GetMPI(&i, &rsa->p) ||
        !GetMPI(&i, &rsa->q) ||
        !GetMPI(&i, &rsa->dp) ||
        !GetMPI(&i, &rsa->dq) ||
        !GetMPI(&i, &rsa->u)) {
        goto error;
    }

    if (version == 1) {
        tag = asn1_der_iterator_next(&i);
        /**
         * According to the standard otherPrimeInfos must be present for
         * version 1. There is no strict verification here, this is to be
         * compatible with the unit test of the kernel. TODO: remove this
         * until linux-kernel's unit-test is fixed;
         */
        if (tag == ASN1_ITERATOR_END) {
            return rsa;
        }
        if (tag != ASN1_ITERATOR_CONSTRUCTED ||
            i.type != ASN1_SEQUENCE) {
            goto error;
        }
    }

    if (asn1_der_iterator_next(&i) != ASN1_ITERATOR_END) {
        goto error;
    }

    return rsa;

error:
    error_setg(errp, "Failed to parse RSA private key");
    qcrypto_akcipher_rsakey_free(rsa);
    return NULL;
}

/**
 *        RsaPubKey ::= SEQUENCE {
 *             n           INTEGER
 *             e           INTEGER
 *         }
 */
static QCryptoAkCipherRSAKey *qcrypto_nettle_rsa_public_key_parse(
    const uint8_t *key, size_t keylen, Error **errp)
{

    QCryptoAkCipherRSAKey *rsa = g_new0(QCryptoAkCipherRSAKey, 1);
    struct asn1_der_iterator i;

    if (asn1_der_iterator_first(&i, keylen, key) != ASN1_ITERATOR_CONSTRUCTED ||
        i.type != ASN1_SEQUENCE ||
        asn1_der_decode_constructed_last(&i) != ASN1_ITERATOR_PRIMITIVE ||
        !DumpMPI(&i, &rsa->n) ||
        !GetMPI(&i, &rsa->e) ||
        asn1_der_iterator_next(&i) != ASN1_ITERATOR_END) {
        goto error;
    }

    return rsa;

error:
    error_setg(errp, "Failed to parse RSA public key");
    qcrypto_akcipher_rsakey_free(rsa);
    return NULL;
}

QCryptoAkCipherRSAKey *qcrypto_akcipher_rsakey_parse(
    QCryptoAkCipherKeyType type, const uint8_t *key,
    size_t keylen, Error **errp)
{
    switch (type) {
    case QCRYPTO_AKCIPHER_KEY_TYPE_PRIVATE:
        return qcrypto_nettle_rsa_private_key_parse(key, keylen, errp);

    case QCRYPTO_AKCIPHER_KEY_TYPE_PUBLIC:
        return qcrypto_nettle_rsa_public_key_parse(key, keylen, errp);

    default:
        error_setg(errp, "Unknown key type: %d", type);
        return NULL;
    }
}
