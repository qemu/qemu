/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QEMU crypto TLS credential support
 *
 * Copyright (c) 2025 Red Hat, Inc.
 */

#include "qemu/osdep.h"
#include "crypto/tlscredsbox.h"
#include "qemu/atomic.h"


static QCryptoTLSCredsBox *
qcrypto_tls_creds_box_new_impl(int type, bool server)
{
    QCryptoTLSCredsBox *credsbox = g_new0(QCryptoTLSCredsBox, 1);
    credsbox->ref = 1;
    credsbox->server = server;
    credsbox->type = type;
    return credsbox;
}


QCryptoTLSCredsBox *
qcrypto_tls_creds_box_new_server(int type)
{
    return qcrypto_tls_creds_box_new_impl(type, true);
}


QCryptoTLSCredsBox *
qcrypto_tls_creds_box_new_client(int type)
{
    return qcrypto_tls_creds_box_new_impl(type, false);
}

static void qcrypto_tls_creds_box_free(QCryptoTLSCredsBox *credsbox)
{
    switch (credsbox->type) {
    case GNUTLS_CRD_CERTIFICATE:
        if (credsbox->data.cert) {
            gnutls_certificate_free_credentials(credsbox->data.cert);
        }
        break;
    case GNUTLS_CRD_PSK:
        if (credsbox->server) {
            if (credsbox->data.pskserver) {
                gnutls_psk_free_server_credentials(credsbox->data.pskserver);
            }
        } else {
            if (credsbox->data.pskclient) {
                gnutls_psk_free_client_credentials(credsbox->data.pskclient);
            }
        }
        break;
    case GNUTLS_CRD_ANON:
        if (credsbox->server) {
            if (credsbox->data.anonserver) {
                gnutls_anon_free_server_credentials(credsbox->data.anonserver);
            }
        } else {
            if (credsbox->data.anonclient) {
                gnutls_anon_free_client_credentials(credsbox->data.anonclient);
            }
        }
        break;
    default:
        g_assert_not_reached();
    }

    if (credsbox->dh_params) {
        gnutls_dh_params_deinit(credsbox->dh_params);
    }

    g_free(credsbox);
}


void qcrypto_tls_creds_box_ref(QCryptoTLSCredsBox *credsbox)
{
    uint32_t ref = qatomic_fetch_inc(&credsbox->ref);
    /* Assert waaay before the integer overflows */
    g_assert(ref < INT_MAX);
}


void qcrypto_tls_creds_box_unref(QCryptoTLSCredsBox *credsbox)
{
    if (!credsbox) {
        return;
    }

    g_assert(credsbox->ref > 0);

    if (qatomic_fetch_dec(&credsbox->ref) == 1) {
        qcrypto_tls_creds_box_free(credsbox);
    }

}

