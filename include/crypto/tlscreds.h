/*
 * QEMU crypto TLS credential support
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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

#ifdef CONFIG_GNUTLS
#include <gnutls/gnutls.h>
#endif

#define TYPE_QCRYPTO_TLS_CREDS "tls-creds"
#define QCRYPTO_TLS_CREDS(obj)                  \
    OBJECT_CHECK(QCryptoTLSCreds, (obj), TYPE_QCRYPTO_TLS_CREDS)

typedef struct QCryptoTLSCreds QCryptoTLSCreds;
typedef struct QCryptoTLSCredsClass QCryptoTLSCredsClass;

#define QCRYPTO_TLS_CREDS_DH_PARAMS "dh-params.pem"


/**
 * QCryptoTLSCreds:
 *
 * The QCryptoTLSCreds object is an abstract base for different
 * types of TLS handshake credentials. Most commonly the
 * QCryptoTLSCredsX509 subclass will be used to provide x509
 * certificate credentials.
 */

struct QCryptoTLSCreds {
    Object parent_obj;
    char *dir;
    QCryptoTLSCredsEndpoint endpoint;
#ifdef CONFIG_GNUTLS
    gnutls_dh_params_t dh_params;
#endif
    bool verifyPeer;
    char *priority;
};


struct QCryptoTLSCredsClass {
    ObjectClass parent_class;
};


#endif /* QCRYPTO_TLSCREDS_H */
