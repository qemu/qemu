/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QEMU crypto TLS credential support
 *
 * Copyright (c) 2025 Red Hat, Inc.
 */

#ifndef QCRYPTO_TLSCREDS_BOX_H
#define QCRYPTO_TLSCREDS_BOX_H

#include "qom/object.h"

#ifdef CONFIG_GNUTLS
#include <gnutls/gnutls.h>
#endif

typedef struct QCryptoTLSCredsBox QCryptoTLSCredsBox;

struct QCryptoTLSCredsBox {
    uint32_t ref;
    bool server;
    int type;
    union {
        void *any;
#ifdef CONFIG_GNUTLS
        /*
         * All of these gnutls_XXXX_credentials_t types are
         * pointers, hence matching the 'any' field above
         */
        gnutls_anon_server_credentials_t anonserver;
        gnutls_anon_client_credentials_t anonclient;
        gnutls_psk_server_credentials_t pskserver;
        gnutls_psk_client_credentials_t pskclient;
        gnutls_certificate_credentials_t cert;
#endif
    } data;
#ifdef CONFIG_GNUTLS
    gnutls_dh_params_t dh_params;
#endif
};

QCryptoTLSCredsBox *qcrypto_tls_creds_box_new_server(int type);
QCryptoTLSCredsBox *qcrypto_tls_creds_box_new_client(int type);
void qcrypto_tls_creds_box_ref(QCryptoTLSCredsBox *credsbox);
void qcrypto_tls_creds_box_unref(QCryptoTLSCredsBox *credsbox);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(QCryptoTLSCredsBox, qcrypto_tls_creds_box_unref);

#endif /* QCRYPTO_TLSCREDS_BOX_H */
