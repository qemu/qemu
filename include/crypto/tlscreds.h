/*
 * QEMU crypto TLS credential support
 *
 * Copyright (c) 2015 Red Hat, Inc.
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

#ifndef QCRYPTO_TLSCREDS_H
#define QCRYPTO_TLSCREDS_H

#include "qapi/qapi-types-crypto.h"
#include "qom/object.h"

#define TYPE_QCRYPTO_TLS_CREDS "tls-creds"
typedef struct QCryptoTLSCreds QCryptoTLSCreds;
typedef struct QCryptoTLSCredsClass QCryptoTLSCredsClass;
DECLARE_OBJ_CHECKERS(QCryptoTLSCreds, QCryptoTLSCredsClass, QCRYPTO_TLS_CREDS,
                     TYPE_QCRYPTO_TLS_CREDS)


#define QCRYPTO_TLS_CREDS_DH_PARAMS "dh-params.pem"


typedef bool (*CryptoTLSCredsReload)(QCryptoTLSCreds *, Error **);
/**
 * QCryptoTLSCreds:
 *
 * The QCryptoTLSCreds object is an abstract base for different
 * types of TLS handshake credentials. Most commonly the
 * QCryptoTLSCredsX509 subclass will be used to provide x509
 * certificate credentials.
 */

struct QCryptoTLSCredsClass {
    ObjectClass parent_class;
    CryptoTLSCredsReload reload;
};

/**
 * qcrypto_tls_creds_check_endpoint:
 * @creds: pointer to a TLS credentials object
 * @endpoint: type of network endpoint that will be using the credentials
 * @errp: pointer to a NULL-initialized error object
 *
 * Check whether the credentials is setup according to
 * the type of @endpoint argument.
 *
 * Returns true if the credentials is setup for the endpoint, false otherwise
 */
bool qcrypto_tls_creds_check_endpoint(QCryptoTLSCreds *creds,
                                      QCryptoTLSCredsEndpoint endpoint,
                                      Error **errp);

#endif /* QCRYPTO_TLSCREDS_H */
