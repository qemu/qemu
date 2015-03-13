/*
 * QEMU crypto TLS anonymous credential support
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

#ifndef QCRYPTO_TLSCRED_ANON_H__
#define QCRYPTO_TLSCRED_ANON_H__

#include "crypto/tlscreds.h"

#define TYPE_QCRYPTO_TLS_CREDS_ANON "tls-creds-anon"
#define QCRYPTO_TLS_CREDS_ANON(obj)                  \
    OBJECT_CHECK(QCryptoTLSCredsAnon, (obj), TYPE_QCRYPTO_TLS_CREDS_ANON)


typedef struct QCryptoTLSCredsAnon QCryptoTLSCredsAnon;
typedef struct QCryptoTLSCredsAnonClass QCryptoTLSCredsAnonClass;

/**
 * QCryptoTLSCredsAnon:
 *
 * The QCryptoTLSCredsAnon object provides a representation
 * of anonymous credentials used perform a TLS handshake.
 * This is primarily provided for backwards compatibility and
 * its use is discouraged as it has poor security characteristics
 * due to lacking MITM attack protection amongst other problems.
 *
 * This is a user creatable object, which can be instantiated
 * via object_new_propv():
 *
 * <example>
 *   <title>Creating anonymous TLS credential objects in code</title>
 *   <programlisting>
 *   Object *obj;
 *   Error *err = NULL;
 *   obj = object_new_propv(TYPE_QCRYPTO_TLS_CREDS_ANON,
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
 *   <title>Creating anonymous TLS credential objects via QMP</title>
 *   <programlisting>
 *    {
 *       "execute": "object-add", "arguments": {
 *          "id": "tlscreds0",
 *          "qom-type": "tls-creds-anon",
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
 *   <title>Creating anonymous TLS credential objects via CLI</title>
 *   <programlisting>
 *  qemu-system-x86_64 -object tls-creds-anon,id=tlscreds0,\
 *          endpoint=server,verify-peer=off,\
 *          dir=/path/to/x509/certdir/
 *   </programlisting>
 * </example>
 *
 */


struct QCryptoTLSCredsAnon {
    QCryptoTLSCreds parent_obj;
#ifdef CONFIG_GNUTLS
    union {
        gnutls_anon_server_credentials_t server;
        gnutls_anon_client_credentials_t client;
    } data;
#endif
};


struct QCryptoTLSCredsAnonClass {
    QCryptoTLSCredsClass parent_class;
};


#endif /* QCRYPTO_TLSCRED_H__ */

