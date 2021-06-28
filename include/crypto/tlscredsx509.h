/*
 * QEMU crypto TLS x509 credential support
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

#ifndef QCRYPTO_TLSCREDSX509_H
#define QCRYPTO_TLSCREDSX509_H

#include "crypto/tlscreds.h"
#include "qom/object.h"

#define TYPE_QCRYPTO_TLS_CREDS_X509 "tls-creds-x509"
typedef struct QCryptoTLSCredsX509 QCryptoTLSCredsX509;
DECLARE_INSTANCE_CHECKER(QCryptoTLSCredsX509, QCRYPTO_TLS_CREDS_X509,
                         TYPE_QCRYPTO_TLS_CREDS_X509)

typedef struct QCryptoTLSCredsX509Class QCryptoTLSCredsX509Class;

#define QCRYPTO_TLS_CREDS_X509_CA_CERT "ca-cert.pem"
#define QCRYPTO_TLS_CREDS_X509_CA_CRL "ca-crl.pem"
#define QCRYPTO_TLS_CREDS_X509_SERVER_KEY "server-key.pem"
#define QCRYPTO_TLS_CREDS_X509_SERVER_CERT "server-cert.pem"
#define QCRYPTO_TLS_CREDS_X509_CLIENT_KEY "client-key.pem"
#define QCRYPTO_TLS_CREDS_X509_CLIENT_CERT "client-cert.pem"


/**
 * QCryptoTLSCredsX509:
 *
 * The QCryptoTLSCredsX509 object provides a representation
 * of x509 credentials used to perform a TLS handshake.
 *
 * This is a user creatable object, which can be instantiated
 * via object_new_propv():
 *
 * <example>
 *   <title>Creating x509 TLS credential objects in code</title>
 *   <programlisting>
 *   Object *obj;
 *   Error *err = NULL;
 *   obj = object_new_propv(TYPE_QCRYPTO_TLS_CREDS_X509,
 *                          "tlscreds0",
 *                          &err,
 *                          "endpoint", "server",
 *                          "dir", "/path/x509/cert/dir",
 *                          "verify-peer", "yes",
 *                          NULL);
 *   </programlisting>
 * </example>
 *
 * Or via QMP:
 *
 * <example>
 *   <title>Creating x509 TLS credential objects via QMP</title>
 *   <programlisting>
 *    {
 *       "execute": "object-add", "arguments": {
 *          "id": "tlscreds0",
 *          "qom-type": "tls-creds-x509",
 *          "props": {
 *             "endpoint": "server",
 *             "dir": "/path/to/x509/cert/dir",
 *             "verify-peer": false
 *          }
 *       }
 *    }
 *   </programlisting>
 * </example>
 *
 *
 * Or via the CLI:
 *
 * <example>
 *   <title>Creating x509 TLS credential objects via CLI</title>
 *   <programlisting>
 *  qemu-system-x86_64 -object tls-creds-x509,id=tlscreds0,\
 *          endpoint=server,verify-peer=off,\
 *          dir=/path/to/x509/certdir/
 *   </programlisting>
 * </example>
 *
 */

struct QCryptoTLSCredsX509Class {
    QCryptoTLSCredsClass parent_class;
};


#endif /* QCRYPTO_TLSCREDSX509_H */
