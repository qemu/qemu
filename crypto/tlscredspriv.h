/*
 * QEMU crypto TLS credential support private helpers
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

#ifndef QCRYPTO_TLSCREDSPRIV_H
#define QCRYPTO_TLSCREDSPRIV_H

#include "crypto/tlscreds.h"

#ifdef CONFIG_GNUTLS
#include <gnutls/gnutls.h>
#endif

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

struct QCryptoTLSCredsAnon {
    QCryptoTLSCreds parent_obj;
#ifdef CONFIG_GNUTLS
    union {
        gnutls_anon_server_credentials_t server;
        gnutls_anon_client_credentials_t client;
    } data;
#endif
};

struct QCryptoTLSCredsPSK {
    QCryptoTLSCreds parent_obj;
    char *username;
#ifdef CONFIG_GNUTLS
    union {
        gnutls_psk_server_credentials_t server;
        gnutls_psk_client_credentials_t client;
    } data;
#endif
};

struct QCryptoTLSCredsX509 {
    QCryptoTLSCreds parent_obj;
#ifdef CONFIG_GNUTLS
    gnutls_certificate_credentials_t data;
#endif
    bool sanityCheck;
    char *passwordid;
};

#ifdef CONFIG_GNUTLS

int qcrypto_tls_creds_get_path(QCryptoTLSCreds *creds,
                               const char *filename,
                               bool required,
                               char **cred,
                               Error **errp);

int qcrypto_tls_creds_get_dh_params_file(QCryptoTLSCreds *creds,
                                         const char *filename,
                                         gnutls_dh_params_t *dh_params,
                                         Error **errp);

#endif

#endif /* QCRYPTO_TLSCREDSPRIV_H */
