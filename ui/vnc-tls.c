/*
 * QEMU VNC display driver: TLS helpers
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

#include "qemu-x509.h"
#include "vnc.h"
#include "qemu/sockets.h"

#if defined(_VNC_DEBUG) && _VNC_DEBUG >= 2
/* Very verbose, so only enabled for _VNC_DEBUG >= 2 */
static void vnc_debug_gnutls_log(int level, const char* str) {
    VNC_DEBUG("%d %s", level, str);
}
#endif /* defined(_VNC_DEBUG) && _VNC_DEBUG >= 2 */


#define DH_BITS 1024
static gnutls_dh_params_t dh_params;

static int vnc_tls_initialize(void)
{
    static int tlsinitialized = 0;

    if (tlsinitialized)
        return 1;

    if (gnutls_global_init () < 0)
        return 0;

    /* XXX ought to re-generate diffie-hellman params periodically */
    if (gnutls_dh_params_init (&dh_params) < 0)
        return 0;
    if (gnutls_dh_params_generate2 (dh_params, DH_BITS) < 0)
        return 0;

#if defined(_VNC_DEBUG) && _VNC_DEBUG >= 2
    gnutls_global_set_log_level(10);
    gnutls_global_set_log_function(vnc_debug_gnutls_log);
#endif

    tlsinitialized = 1;

    return 1;
}

static ssize_t vnc_tls_push(gnutls_transport_ptr_t transport,
                            const void *data,
                            size_t len) {
    struct VncState *vs = (struct VncState *)transport;
    int ret;

 retry:
    ret = send(vs->csock, data, len, 0);
    if (ret < 0) {
        if (errno == EINTR)
            goto retry;
        return -1;
    }
    return ret;
}


static ssize_t vnc_tls_pull(gnutls_transport_ptr_t transport,
                            void *data,
                            size_t len) {
    struct VncState *vs = (struct VncState *)transport;
    int ret;

 retry:
    ret = qemu_recv(vs->csock, data, len, 0);
    if (ret < 0) {
        if (errno == EINTR)
            goto retry;
        return -1;
    }
    return ret;
}


static gnutls_anon_server_credentials_t vnc_tls_initialize_anon_cred(void)
{
    gnutls_anon_server_credentials_t anon_cred;
    int ret;

    if ((ret = gnutls_anon_allocate_server_credentials(&anon_cred)) < 0) {
        VNC_DEBUG("Cannot allocate credentials %s\n", gnutls_strerror(ret));
        return NULL;
    }

    gnutls_anon_set_server_dh_params(anon_cred, dh_params);

    return anon_cred;
}


static gnutls_certificate_credentials_t vnc_tls_initialize_x509_cred(VncDisplay *vd)
{
    gnutls_certificate_credentials_t x509_cred;
    int ret;

    if (!vd->tls.x509cacert) {
        VNC_DEBUG("No CA x509 certificate specified\n");
        return NULL;
    }
    if (!vd->tls.x509cert) {
        VNC_DEBUG("No server x509 certificate specified\n");
        return NULL;
    }
    if (!vd->tls.x509key) {
        VNC_DEBUG("No server private key specified\n");
        return NULL;
    }

    if ((ret = gnutls_certificate_allocate_credentials(&x509_cred)) < 0) {
        VNC_DEBUG("Cannot allocate credentials %s\n", gnutls_strerror(ret));
        return NULL;
    }
    if ((ret = gnutls_certificate_set_x509_trust_file(x509_cred,
                                                      vd->tls.x509cacert,
                                                      GNUTLS_X509_FMT_PEM)) < 0) {
        VNC_DEBUG("Cannot load CA certificate %s\n", gnutls_strerror(ret));
        gnutls_certificate_free_credentials(x509_cred);
        return NULL;
    }

    if ((ret = gnutls_certificate_set_x509_key_file (x509_cred,
                                                     vd->tls.x509cert,
                                                     vd->tls.x509key,
                                                     GNUTLS_X509_FMT_PEM)) < 0) {
        VNC_DEBUG("Cannot load certificate & key %s\n", gnutls_strerror(ret));
        gnutls_certificate_free_credentials(x509_cred);
        return NULL;
    }

    if (vd->tls.x509cacrl) {
        if ((ret = gnutls_certificate_set_x509_crl_file(x509_cred,
                                                        vd->tls.x509cacrl,
                                                        GNUTLS_X509_FMT_PEM)) < 0) {
            VNC_DEBUG("Cannot load CRL %s\n", gnutls_strerror(ret));
            gnutls_certificate_free_credentials(x509_cred);
            return NULL;
        }
    }

    gnutls_certificate_set_dh_params (x509_cred, dh_params);

    return x509_cred;
}


int vnc_tls_validate_certificate(struct VncState *vs)
{
    int ret;
    unsigned int status;
    const gnutls_datum_t *certs;
    unsigned int nCerts, i;
    time_t now;

    VNC_DEBUG("Validating client certificate\n");
    if ((ret = gnutls_certificate_verify_peers2 (vs->tls.session, &status)) < 0) {
        VNC_DEBUG("Verify failed %s\n", gnutls_strerror(ret));
        return -1;
    }

    if ((now = time(NULL)) == ((time_t)-1)) {
        return -1;
    }

    if (status != 0) {
        if (status & GNUTLS_CERT_INVALID)
            VNC_DEBUG("The certificate is not trusted.\n");

        if (status & GNUTLS_CERT_SIGNER_NOT_FOUND)
            VNC_DEBUG("The certificate hasn't got a known issuer.\n");

        if (status & GNUTLS_CERT_REVOKED)
            VNC_DEBUG("The certificate has been revoked.\n");

        if (status & GNUTLS_CERT_INSECURE_ALGORITHM)
            VNC_DEBUG("The certificate uses an insecure algorithm\n");

        return -1;
    } else {
        VNC_DEBUG("Certificate is valid!\n");
    }

    /* Only support x509 for now */
    if (gnutls_certificate_type_get(vs->tls.session) != GNUTLS_CRT_X509)
        return -1;

    if (!(certs = gnutls_certificate_get_peers(vs->tls.session, &nCerts)))
        return -1;

    for (i = 0 ; i < nCerts ; i++) {
        gnutls_x509_crt_t cert;
        VNC_DEBUG ("Checking certificate chain %d\n", i);
        if (gnutls_x509_crt_init (&cert) < 0)
            return -1;

        if (gnutls_x509_crt_import(cert, &certs[i], GNUTLS_X509_FMT_DER) < 0) {
            gnutls_x509_crt_deinit (cert);
            return -1;
        }

        if (gnutls_x509_crt_get_expiration_time (cert) < now) {
            VNC_DEBUG("The certificate has expired\n");
            gnutls_x509_crt_deinit (cert);
            return -1;
        }

        if (gnutls_x509_crt_get_activation_time (cert) > now) {
            VNC_DEBUG("The certificate is not yet activated\n");
            gnutls_x509_crt_deinit (cert);
            return -1;
        }

        if (gnutls_x509_crt_get_activation_time (cert) > now) {
            VNC_DEBUG("The certificate is not yet activated\n");
            gnutls_x509_crt_deinit (cert);
            return -1;
        }

        if (i == 0) {
            size_t dnameSize = 1024;
            vs->tls.dname = g_malloc(dnameSize);
        requery:
            if ((ret = gnutls_x509_crt_get_dn (cert, vs->tls.dname, &dnameSize)) != 0) {
                if (ret == GNUTLS_E_SHORT_MEMORY_BUFFER) {
                    vs->tls.dname = g_realloc(vs->tls.dname, dnameSize);
                    goto requery;
                }
                gnutls_x509_crt_deinit (cert);
                VNC_DEBUG("Cannot get client distinguished name: %s",
                          gnutls_strerror (ret));
                return -1;
            }

            if (vs->vd->tls.x509verify) {
                int allow;
                if (!vs->vd->tls.acl) {
                    VNC_DEBUG("no ACL activated, allowing access");
                    gnutls_x509_crt_deinit (cert);
                    continue;
                }

                allow = qemu_acl_party_is_allowed(vs->vd->tls.acl,
                                                  vs->tls.dname);

                VNC_DEBUG("TLS x509 ACL check for %s is %s\n",
                          vs->tls.dname, allow ? "allowed" : "denied");
                if (!allow) {
                    gnutls_x509_crt_deinit (cert);
                    return -1;
                }
            }
        }

        gnutls_x509_crt_deinit (cert);
    }

    return 0;
}

#if defined(GNUTLS_VERSION_NUMBER) && \
    GNUTLS_VERSION_NUMBER >= 0x020200 /* 2.2.0 */

static int vnc_set_gnutls_priority(gnutls_session_t s, int x509)
{
    const char *priority = x509 ? "NORMAL" : "NORMAL:+ANON-DH";
    int rc;

    rc = gnutls_priority_set_direct(s, priority, NULL);
    if (rc != GNUTLS_E_SUCCESS) {
        return -1;
    }
    return 0;
}

#else

static int vnc_set_gnutls_priority(gnutls_session_t s, int x509)
{
    static const int cert_types[] = { GNUTLS_CRT_X509, 0 };
    static const int protocols[] = {
        GNUTLS_TLS1_1, GNUTLS_TLS1_0, GNUTLS_SSL3, 0
    };
    static const int kx_anon[] = { GNUTLS_KX_ANON_DH, 0 };
    static const int kx_x509[] = {
        GNUTLS_KX_DHE_DSS, GNUTLS_KX_RSA,
        GNUTLS_KX_DHE_RSA, GNUTLS_KX_SRP, 0
    };
    int rc;

    rc = gnutls_kx_set_priority(s, x509 ? kx_x509 : kx_anon);
    if (rc != GNUTLS_E_SUCCESS) {
        return -1;
    }

    rc = gnutls_certificate_type_set_priority(s, cert_types);
    if (rc != GNUTLS_E_SUCCESS) {
        return -1;
    }

    rc = gnutls_protocol_set_priority(s, protocols);
    if (rc != GNUTLS_E_SUCCESS) {
        return -1;
    }
    return 0;
}

#endif

int vnc_tls_client_setup(struct VncState *vs,
                         int needX509Creds) {
    VncStateTLS *tls;

    VNC_DEBUG("Do TLS setup\n");
#ifdef CONFIG_VNC_WS
    if (vs->websocket) {
        tls = &vs->ws_tls;
    } else
#endif /* CONFIG_VNC_WS */
    {
        tls = &vs->tls;
    }
    if (vnc_tls_initialize() < 0) {
        VNC_DEBUG("Failed to init TLS\n");
        vnc_client_error(vs);
        return -1;
    }
    if (tls->session == NULL) {
        if (gnutls_init(&tls->session, GNUTLS_SERVER) < 0) {
            vnc_client_error(vs);
            return -1;
        }

        if (gnutls_set_default_priority(tls->session) < 0) {
            gnutls_deinit(tls->session);
            tls->session = NULL;
            vnc_client_error(vs);
            return -1;
        }

        if (vnc_set_gnutls_priority(tls->session, needX509Creds) < 0) {
            gnutls_deinit(tls->session);
            tls->session = NULL;
            vnc_client_error(vs);
            return -1;
        }

        if (needX509Creds) {
            gnutls_certificate_server_credentials x509_cred = vnc_tls_initialize_x509_cred(vs->vd);
            if (!x509_cred) {
                gnutls_deinit(tls->session);
                tls->session = NULL;
                vnc_client_error(vs);
                return -1;
            }
            if (gnutls_credentials_set(tls->session, GNUTLS_CRD_CERTIFICATE, x509_cred) < 0) {
                gnutls_deinit(tls->session);
                tls->session = NULL;
                gnutls_certificate_free_credentials(x509_cred);
                vnc_client_error(vs);
                return -1;
            }
            if (vs->vd->tls.x509verify) {
                VNC_DEBUG("Requesting a client certificate\n");
                gnutls_certificate_server_set_request (tls->session, GNUTLS_CERT_REQUEST);
            }

        } else {
            gnutls_anon_server_credentials_t anon_cred = vnc_tls_initialize_anon_cred();
            if (!anon_cred) {
                gnutls_deinit(tls->session);
                tls->session = NULL;
                vnc_client_error(vs);
                return -1;
            }
            if (gnutls_credentials_set(tls->session, GNUTLS_CRD_ANON, anon_cred) < 0) {
                gnutls_deinit(tls->session);
                tls->session = NULL;
                gnutls_anon_free_server_credentials(anon_cred);
                vnc_client_error(vs);
                return -1;
            }
        }

        gnutls_transport_set_ptr(tls->session, (gnutls_transport_ptr_t)vs);
        gnutls_transport_set_push_function(tls->session, vnc_tls_push);
        gnutls_transport_set_pull_function(tls->session, vnc_tls_pull);
    }
    return 0;
}


void vnc_tls_client_cleanup(struct VncState *vs)
{
    if (vs->tls.session) {
        gnutls_deinit(vs->tls.session);
        vs->tls.session = NULL;
    }
    vs->tls.wiremode = VNC_WIREMODE_CLEAR;
    g_free(vs->tls.dname);
#ifdef CONFIG_VNC_WS
    if (vs->ws_tls.session) {
        gnutls_deinit(vs->ws_tls.session);
        vs->ws_tls.session = NULL;
    }
    vs->ws_tls.wiremode = VNC_WIREMODE_CLEAR;
    g_free(vs->ws_tls.dname);
#endif /* CONFIG_VNC_WS */
}



static int vnc_set_x509_credential(VncDisplay *vd,
                                   const char *certdir,
                                   const char *filename,
                                   char **cred,
                                   int ignoreMissing)
{
    struct stat sb;

    g_free(*cred);
    *cred = NULL;

    *cred = g_malloc(strlen(certdir) + strlen(filename) + 2);

    strcpy(*cred, certdir);
    strcat(*cred, "/");
    strcat(*cred, filename);

    VNC_DEBUG("Check %s\n", *cred);
    if (stat(*cred, &sb) < 0) {
        g_free(*cred);
        *cred = NULL;
        if (ignoreMissing && errno == ENOENT)
            return 0;
        return -1;
    }

    return 0;
}


int vnc_tls_set_x509_creds_dir(VncDisplay *vd,
                               const char *certdir)
{
    if (vnc_set_x509_credential(vd, certdir, X509_CA_CERT_FILE, &vd->tls.x509cacert, 0) < 0)
        goto cleanup;
    if (vnc_set_x509_credential(vd, certdir, X509_CA_CRL_FILE, &vd->tls.x509cacrl, 1) < 0)
        goto cleanup;
    if (vnc_set_x509_credential(vd, certdir, X509_SERVER_CERT_FILE, &vd->tls.x509cert, 0) < 0)
        goto cleanup;
    if (vnc_set_x509_credential(vd, certdir, X509_SERVER_KEY_FILE, &vd->tls.x509key, 0) < 0)
        goto cleanup;

    return 0;

 cleanup:
    g_free(vd->tls.x509cacert);
    g_free(vd->tls.x509cacrl);
    g_free(vd->tls.x509cert);
    g_free(vd->tls.x509key);
    vd->tls.x509cacert = vd->tls.x509cacrl = vd->tls.x509cert = vd->tls.x509key = NULL;
    return -1;
}

