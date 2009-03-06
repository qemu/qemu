/*
 * QEMU VNC display driver. TLS helpers
 *
 * Copyright (C) 2006 Anthony Liguori <anthony@codemonkey.ws>
 * Copyright (C) 2006 Fabrice Bellard
 * Copyright (C) 2009 Red Hat, Inc
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


#ifndef __QEMU_VNC_TLS_H__
#define __QEMU_VNC_TLS_H__

#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

#include "acl.h"

enum {
    VNC_WIREMODE_CLEAR,
    VNC_WIREMODE_TLS,
};

typedef struct VncDisplayTLS VncDisplayTLS;
typedef struct VncStateTLS VncStateTLS;

/* Server state */
struct VncDisplayTLS {
    int x509verify; /* Non-zero if server requests & validates client cert */
    qemu_acl *acl;

    /* Paths to x509 certs/keys */
    char *x509cacert;
    char *x509cacrl;
    char *x509cert;
    char *x509key;
};

/* Per client state */
struct VncStateTLS {
    /* Whether data is being TLS encrypted yet */
    int wiremode;
    gnutls_session_t session;

    /* Client's Distinguished Name from the x509 cert */
    char *dname;
};

int vnc_tls_client_setup(VncState *vs, int x509Creds);
void vnc_tls_client_cleanup(VncState *vs);

int vnc_tls_validate_certificate(VncState *vs);

int vnc_tls_set_x509_creds_dir(VncDisplay *vd,
			       const char *path);


#endif /* __QEMU_VNC_TLS_H__ */

