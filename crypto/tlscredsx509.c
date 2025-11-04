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

#include "qemu/osdep.h"
#include "crypto/tlscredsx509.h"
#include "tlscredspriv.h"
#include "crypto/secret.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qom/object_interfaces.h"
#include "trace.h"


struct QCryptoTLSCredsX509 {
    QCryptoTLSCreds parent_obj;
    bool sanityCheck;
    char *passwordid;
};

#ifdef CONFIG_GNUTLS

#include <gnutls/gnutls.h>
#include <gnutls/x509.h>


typedef struct QCryptoTLSCredsX509IdentFiles QCryptoTLSCredsX509IdentFiles;
struct QCryptoTLSCredsX509IdentFiles {
    char *certpath;
    char *keypath;
    gnutls_x509_crt_t *certs;
    unsigned int ncerts;
    gnutls_x509_privkey_t key;
};

typedef struct QCryptoTLSCredsX509Files QCryptoTLSCredsX509Files;
struct QCryptoTLSCredsX509Files {
    char *cacertpath;
    gnutls_x509_crt_t *cacerts;
    unsigned int ncacerts;

    QCryptoTLSCredsX509IdentFiles **identities;
    size_t nidentities;
};

static QCryptoTLSCredsX509Files *
qcrypto_tls_creds_x509_files_new(void)
{
    return g_new0(QCryptoTLSCredsX509Files, 1);
}


static void
qcrypto_tls_creds_x509_ident_files_free(QCryptoTLSCredsX509IdentFiles *files)
{
    size_t i;
    for (i = 0; i < files->ncerts; i++) {
        gnutls_x509_crt_deinit(files->certs[i]);
    }
    gnutls_x509_privkey_deinit(files->key);
    g_free(files->certs);
    g_free(files->certpath);
    g_free(files->keypath);
    g_free(files);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(QCryptoTLSCredsX509IdentFiles,
                              qcrypto_tls_creds_x509_ident_files_free);


static void
qcrypto_tls_creds_x509_files_free(QCryptoTLSCredsX509Files *files)
{
    size_t i;
    for (i = 0; i < files->ncacerts; i++) {
        gnutls_x509_crt_deinit(files->cacerts[i]);
    }
    g_free(files->cacerts);
    g_free(files->cacertpath);
    for (i = 0; i < files->nidentities; i++) {
        qcrypto_tls_creds_x509_ident_files_free(files->identities[i]);
    }
    g_free(files->identities);
    g_free(files);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(QCryptoTLSCredsX509Files,
                              qcrypto_tls_creds_x509_files_free);

static int
qcrypto_tls_creds_check_cert_times(gnutls_x509_crt_t cert,
                                   const char *certFile,
                                   bool isServer,
                                   bool isCA,
                                   Error **errp)
{
    time_t now = time(NULL);

    if (now == ((time_t)-1)) {
        error_setg_errno(errp, errno, "cannot get current time");
        return -1;
    }

    if (gnutls_x509_crt_get_expiration_time(cert) < now) {
        error_setg(errp,
                   (isCA ?
                    "The CA certificate %s has expired" :
                    (isServer ?
                     "The server certificate %s has expired" :
                     "The client certificate %s has expired")),
                   certFile);
        return -1;
    }

    if (gnutls_x509_crt_get_activation_time(cert) > now) {
        error_setg(errp,
                   (isCA ?
                    "The CA certificate %s is not yet active" :
                    (isServer ?
                     "The server certificate %s is not yet active" :
                     "The client certificate %s is not yet active")),
                   certFile);
        return -1;
    }

    return 0;
}


static int
qcrypto_tls_creds_check_cert_basic_constraints(QCryptoTLSCredsX509 *creds,
                                               gnutls_x509_crt_t cert,
                                               const char *certFile,
                                               bool isServer,
                                               bool isCA,
                                               Error **errp)
{
    int status;

    status = gnutls_x509_crt_get_basic_constraints(cert, NULL, NULL, NULL);
    trace_qcrypto_tls_creds_x509_check_basic_constraints(
        creds, certFile, status);

    if (status > 0) { /* It is a CA cert */
        if (!isCA) {
            error_setg(errp, isServer ?
                       "The certificate %s basic constraints show a CA, "
                       "but we need one for a server" :
                       "The certificate %s basic constraints show a CA, "
                       "but we need one for a client",
                       certFile);
            return -1;
        }
    } else if (status == 0) { /* It is not a CA cert */
        if (isCA) {
            error_setg(errp,
                       "The certificate %s basic constraints do not "
                       "show a CA",
                       certFile);
            return -1;
        }
    } else if (status == GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE) {
        /* Missing basicConstraints */
        if (isCA) {
            error_setg(errp,
                       "The certificate %s is missing basic constraints "
                       "for a CA",
                       certFile);
            return -1;
        }
    } else { /* General error */
        error_setg(errp,
                   "Unable to query certificate %s basic constraints: %s",
                   certFile, gnutls_strerror(status));
        return -1;
    }

    return 0;
}


static int
qcrypto_tls_creds_check_cert_key_usage(QCryptoTLSCredsX509 *creds,
                                       gnutls_x509_crt_t cert,
                                       const char *certFile,
                                       bool isCA,
                                       Error **errp)
{
    int status;
    unsigned int usage = 0;
    unsigned int critical = 0;

    status = gnutls_x509_crt_get_key_usage(cert, &usage, &critical);
    trace_qcrypto_tls_creds_x509_check_key_usage(
        creds, certFile, status, usage, critical);

    if (status < 0) {
        if (status == GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE) {
            usage = isCA ? GNUTLS_KEY_KEY_CERT_SIGN :
                GNUTLS_KEY_DIGITAL_SIGNATURE;
        } else {
            error_setg(errp,
                       "Unable to query certificate %s key usage: %s",
                       certFile, gnutls_strerror(status));
            return -1;
        }
    }

    if (isCA) {
        if (!(usage & GNUTLS_KEY_KEY_CERT_SIGN)) {
            if (critical) {
                error_setg(errp,
                           "Certificate %s usage does not permit "
                           "certificate signing", certFile);
                return -1;
            }
        }
    } else {
        if (!(usage & GNUTLS_KEY_DIGITAL_SIGNATURE)) {
            if (critical) {
                error_setg(errp,
                           "Certificate %s usage does not permit digital "
                           "signature", certFile);
                return -1;
            }
        }
    }

    return 0;
}


static int
qcrypto_tls_creds_check_cert_key_purpose(QCryptoTLSCredsX509 *creds,
                                         gnutls_x509_crt_t cert,
                                         const char *certFile,
                                         bool isServer,
                                         Error **errp)
{
    int status;
    size_t i;
    unsigned int purposeCritical;
    unsigned int critical;
    char *buffer = NULL;
    size_t size;
    bool allowClient = false, allowServer = false;

    critical = 0;
    for (i = 0; ; i++) {
        size = 0;
        status = gnutls_x509_crt_get_key_purpose_oid(cert, i, buffer,
                                                     &size, NULL);

        if (status == GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE) {

            /* If there is no data at all, then we must allow
               client/server to pass */
            if (i == 0) {
                allowServer = allowClient = true;
            }
            break;
        }
        if (status != GNUTLS_E_SHORT_MEMORY_BUFFER) {
            error_setg(errp,
                       "Unable to query certificate %s key purpose: %s",
                       certFile, gnutls_strerror(status));
            return -1;
        }

        buffer = g_new0(char, size);

        status = gnutls_x509_crt_get_key_purpose_oid(cert, i, buffer,
                                                     &size, &purposeCritical);

        if (status < 0) {
            trace_qcrypto_tls_creds_x509_check_key_purpose(
                creds, certFile, status, "<none>", purposeCritical);
            g_free(buffer);
            error_setg(errp,
                       "Unable to query certificate %s key purpose: %s",
                       certFile, gnutls_strerror(status));
            return -1;
        }
        trace_qcrypto_tls_creds_x509_check_key_purpose(
            creds, certFile, status, buffer, purposeCritical);
        if (purposeCritical) {
            critical = true;
        }

        if (g_str_equal(buffer, GNUTLS_KP_TLS_WWW_SERVER)) {
            allowServer = true;
        } else if (g_str_equal(buffer, GNUTLS_KP_TLS_WWW_CLIENT)) {
            allowClient = true;
        } else if (g_str_equal(buffer, GNUTLS_KP_ANY)) {
            allowServer = allowClient = true;
        }

        g_free(buffer);
        buffer = NULL;
    }

    if (isServer) {
        if (!allowServer) {
            if (critical) {
                error_setg(errp,
                           "Certificate %s purpose does not allow "
                           "use with a TLS server", certFile);
                return -1;
            }
        }
    } else {
        if (!allowClient) {
            if (critical) {
                error_setg(errp,
                           "Certificate %s purpose does not allow use "
                           "with a TLS client", certFile);
                return -1;
            }
        }
    }

    return 0;
}


static int
qcrypto_tls_creds_check_cert(QCryptoTLSCredsX509 *creds,
                             gnutls_x509_crt_t cert,
                             const char *certFile,
                             bool isServer,
                             bool isCA,
                             Error **errp)
{
    if (qcrypto_tls_creds_check_cert_times(cert, certFile,
                                           isServer, isCA,
                                           errp) < 0) {
        return -1;
    }

    if (qcrypto_tls_creds_check_cert_basic_constraints(creds,
                                                       cert, certFile,
                                                       isServer, isCA,
                                                       errp) < 0) {
        return -1;
    }

    if (qcrypto_tls_creds_check_cert_key_usage(creds,
                                               cert, certFile,
                                               isCA, errp) < 0) {
        return -1;
    }

    if (!isCA &&
        qcrypto_tls_creds_check_cert_key_purpose(creds,
                                                 cert, certFile,
                                                 isServer, errp) < 0) {
        return -1;
    }

    return 0;
}

static int
qcrypto_tls_creds_check_authority_chain(QCryptoTLSCredsX509 *creds,
                                        QCryptoTLSCredsX509Files *files,
                                        gnutls_x509_crt_t *certs,
                                        unsigned int ncerts,
                                        bool isServer,
                                        Error **errp)
{
    gnutls_x509_crt_t cert_to_check = certs[ncerts - 1];
    int retval = 0;
    gnutls_datum_t dn = {}, dnissuer = {};

    for (int i = 0; i < (ncerts - 1); i++) {
        if (!gnutls_x509_crt_check_issuer(certs[i], certs[i + 1])) {
            retval = gnutls_x509_crt_get_dn2(certs[i], &dn);
            if (retval < 0) {
                error_setg(errp, "Unable to fetch cert DN: %s",
                           gnutls_strerror(retval));
                return -1;
            }
            retval = gnutls_x509_crt_get_dn2(certs[i + 1], &dnissuer);
            if (retval < 0) {
                g_free(dn.data);
                error_setg(errp, "Unable to fetch cert DN: %s",
                           gnutls_strerror(retval));
                return -1;
            }
            error_setg(errp, "Cert '%s' does not match issuer of cert '%s'",
                       dnissuer.data, dn.data);
            g_free(dn.data);
            g_free(dnissuer.data);
            return -1;
        }
    }

    for (;;) {
        gnutls_x509_crt_t cert_issuer = NULL;

        if (gnutls_x509_crt_check_issuer(cert_to_check,
                                         cert_to_check)) {
            /*
             * The cert is self-signed indicating we have
             * reached the root of trust.
             */
            return qcrypto_tls_creds_check_cert(
                creds, cert_to_check, files->cacertpath,
                isServer, true, errp);
        }
        for (int i = 0; i < files->ncacerts; i++) {
            if (gnutls_x509_crt_check_issuer(cert_to_check,
                                             files->cacerts[i])) {
                cert_issuer = files->cacerts[i];
                break;
            }
        }
        if (!cert_issuer) {
            break;
        }

        if (qcrypto_tls_creds_check_cert(creds, cert_issuer, files->cacertpath,
                                         isServer, true, errp) < 0) {
            return -1;
        }

        cert_to_check = cert_issuer;
    }

    retval = gnutls_x509_crt_get_dn2(cert_to_check, &dn);
    if (retval < 0) {
        error_setg(errp, "Unable to fetch cert DN: %s",
                   gnutls_strerror(retval));
        return -1;
    }
    error_setg(errp, "Cert '%s' has no issuer in CA chain", dn.data);
    g_free(dn.data);
    return -1;
}

static int
qcrypto_tls_creds_check_cert_pair(QCryptoTLSCredsX509Files *files,
                                  gnutls_x509_crt_t *certs,
                                  size_t ncerts,
                                  const char *certFile,
                                  bool isServer,
                                  Error **errp)
{
    unsigned int status;

    if (gnutls_x509_crt_list_verify(certs, ncerts,
                                    files->cacerts, files->ncacerts,
                                    NULL, 0,
                                    0, &status) < 0) {
        error_setg(errp, isServer ?
                   "Unable to verify server certificate %s against "
                   "CA certificate %s" :
                   "Unable to verify client certificate %s against "
                   "CA certificate %s",
                   certFile, files->cacertpath);
        return -1;
    }

    if (status != 0) {
        const char *reason = "Invalid certificate";

        if (status & GNUTLS_CERT_INVALID) {
            reason = "The certificate is not trusted";
        }

        if (status & GNUTLS_CERT_SIGNER_NOT_FOUND) {
            reason = "The certificate hasn't got a known issuer";
        }

        if (status & GNUTLS_CERT_REVOKED) {
            reason = "The certificate has been revoked";
        }

        if (status & GNUTLS_CERT_INSECURE_ALGORITHM) {
            reason = "The certificate uses an insecure algorithm";
        }

        error_setg(errp,
                   "Our own certificate %s failed validation against %s: %s",
                   certFile, files->cacertpath, reason);
        return -1;
    }

    return 0;
}


static int
qcrypto_tls_creds_load_cert_list(QCryptoTLSCredsX509 *creds,
                                 const char *certFile,
                                 gnutls_x509_crt_t **certs,
                                 unsigned int *ncerts,
                                 Error **errp)
{
    gnutls_datum_t data;
    g_autofree char *buf = NULL;
    gsize buflen;
    GError *gerr = NULL;
    int ret;

    *ncerts = 0;
    trace_qcrypto_tls_creds_x509_load_cert_list(creds, certFile);

    if (!g_file_get_contents(certFile, &buf, &buflen, &gerr)) {
        error_setg(errp, "Cannot load CA cert list %s: %s",
                   certFile, gerr->message);
        g_error_free(gerr);
        return -1;
    }

    data.data = (unsigned char *)buf;
    data.size = strlen(buf);

    ret = gnutls_x509_crt_list_import2(certs, ncerts, &data,
                                       GNUTLS_X509_FMT_PEM, 0);
    if (ret < 0) {
        error_setg(errp, "Unable to import certificate %s: %s",
                   certFile, gnutls_strerror(ret));
        return -1;
    }

    return 0;
}


static int
qcrypto_tls_creds_load_privkey(QCryptoTLSCredsX509 *creds,
                               const char *keyFile,
                               gnutls_x509_privkey_t *key,
                               Error **errp)
{
    gnutls_datum_t data;
    g_autofree char *buf = NULL;
    g_autofree char *password = NULL;
    gsize buflen;
    GError *gerr = NULL;
    int ret;

    ret = gnutls_x509_privkey_init(key);
    if (ret < 0) {
        error_setg(errp, "Unable to initialize private key: %s",
                   gnutls_strerror(ret));
        return -1;
    }

    if (!g_file_get_contents(keyFile, &buf, &buflen, &gerr)) {
        error_setg(errp, "Cannot load private key %s: %s",
                   keyFile, gerr->message);
        g_error_free(gerr);
        return -1;
    }

    data.data = (unsigned char *)buf;
    data.size = strlen(buf);

    if (creds->passwordid) {
        password = qcrypto_secret_lookup_as_utf8(creds->passwordid,
                                                 errp);
        if (!password) {
            return -1;
        }
    }

    if (gnutls_x509_privkey_import2(*key, &data,
                                    GNUTLS_X509_FMT_PEM,
                                    password, 0) < 0) {
        error_setg(errp, "Unable to import private key %s", keyFile);
        return -1;
    }

    return 0;
}


static int
qcrypto_tls_creds_x509_sanity_check_identity(QCryptoTLSCredsX509 *creds,
                                             QCryptoTLSCredsX509Files *files,
                                             QCryptoTLSCredsX509IdentFiles *ifiles,
                                             bool isServer,
                                             Error **errp)
{
    size_t i;

    for (i = 0; i < ifiles->ncerts; i++) {
        if (qcrypto_tls_creds_check_cert(creds,
                                         ifiles->certs[i], ifiles->certpath,
                                         isServer, i != 0, errp) < 0) {
            return -1;
        }
    }

    if (ifiles->ncerts &&
        qcrypto_tls_creds_check_authority_chain(creds, files,
                                                ifiles->certs, ifiles->ncerts,
                                                isServer, errp) < 0) {
        return -1;
    }

    if (ifiles->ncerts &&
        qcrypto_tls_creds_check_cert_pair(files, ifiles->certs, ifiles->ncerts,
                                          ifiles->certpath, isServer, errp) < 0) {
        return -1;
    }

    return 0;
}


static int
qcrypto_tls_creds_x509_sanity_check(QCryptoTLSCredsX509 *creds,
                                    QCryptoTLSCredsX509Files *files,
                                    bool isServer,
                                    Error **errp)
{
    size_t i;
    for (i = 0; i < files->nidentities; i++) {
        if (qcrypto_tls_creds_x509_sanity_check_identity(creds,
                                                         files,
                                                         files->identities[i],
                                                         isServer,
                                                         errp) < 0) {
            return -1;
        }
    }
    return 0;
}


static int
qcrypto_tls_creds_x509_load_ca(QCryptoTLSCredsX509 *creds,
                               QCryptoTLSCredsBox *box,
                               QCryptoTLSCredsX509Files *files,
                               bool isServer,
                               Error **errp)
{
    int ret;

    if (qcrypto_tls_creds_get_path(&creds->parent_obj,
                                   QCRYPTO_TLS_CREDS_X509_CA_CERT,
                                   true, &files->cacertpath, errp) < 0) {
        return -1;
    }

    if (qcrypto_tls_creds_load_cert_list(creds,
                                         files->cacertpath,
                                         &files->cacerts,
                                         &files->ncacerts,
                                         errp) < 0) {
        return -1;
    }

    ret = gnutls_certificate_set_x509_trust(box->data.cert,
                                            files->cacerts, files->ncacerts);
    if (ret < 0) {
        error_setg(errp, "Cannot set CA certificate '%s': %s",
                   files->cacertpath, gnutls_strerror(ret));
        return -1;
    }

    return 0;
}


static QCryptoTLSCredsX509IdentFiles *
qcrypto_tls_creds_x509_load_identity(QCryptoTLSCredsX509 *creds,
                                     QCryptoTLSCredsBox *box,
                                     const char *certbase,
                                     const char *keybase,
                                     Error **errp)
{
    g_autoptr(QCryptoTLSCredsX509IdentFiles) files =
        g_new0(QCryptoTLSCredsX509IdentFiles, 1);
    int ret;

    if (qcrypto_tls_creds_get_path(&creds->parent_obj, certbase,
                                   false, &files->certpath, errp) < 0 ||
        qcrypto_tls_creds_get_path(&creds->parent_obj, keybase,
                                   false, &files->keypath, errp) < 0) {
        return NULL;
    }

    if (!files->certpath &&
        !files->keypath) {
        return NULL;
    }
    if (files->certpath && !files->keypath) {
        g_autofree char *keypath =
            qcrypto_tls_creds_build_path(&creds->parent_obj, keybase);
        error_setg(errp, "Cert '%s' without corresponding key '%s'",
                   files->certpath, keypath);
        return NULL;
    }
    if (!files->certpath && files->keypath) {
        g_autofree char *certpath =
            qcrypto_tls_creds_build_path(&creds->parent_obj, certbase);
        error_setg(errp, "Key '%s' without corresponding cert '%s'",
                   files->keypath, certpath);
        return NULL;
    }

    if (qcrypto_tls_creds_load_cert_list(creds,
                                         files->certpath,
                                         &files->certs,
                                         &files->ncerts,
                                         errp) < 0) {
        return NULL;
    }

    if (qcrypto_tls_creds_load_privkey(creds,
                                       files->keypath,
                                       &files->key,
                                       errp) < 0) {
        return NULL;
    }

    ret = gnutls_certificate_set_x509_key(box->data.cert,
                                          files->certs,
                                          files->ncerts,
                                          files->key);
    if (ret < 0) {
        error_setg(errp, "Cannot set certificate '%s' & key '%s': %s",
                   files->certpath, files->keypath, gnutls_strerror(ret));
        return NULL;
    }
    return g_steal_pointer(&files);
}


static int
qcrypto_tls_creds_x509_load_identities(QCryptoTLSCredsX509 *creds,
                                       QCryptoTLSCredsBox *box,
                                       QCryptoTLSCredsX509Files *files,
                                       bool isServer,
                                       Error **errp)
{
    ERRP_GUARD();
    QCryptoTLSCredsX509IdentFiles *ifiles;
    size_t i;

    ifiles = qcrypto_tls_creds_x509_load_identity(
        creds, box,
        isServer ?
        QCRYPTO_TLS_CREDS_X509_SERVER_CERT :
        QCRYPTO_TLS_CREDS_X509_CLIENT_CERT,
        isServer ?
        QCRYPTO_TLS_CREDS_X509_SERVER_KEY :
        QCRYPTO_TLS_CREDS_X509_CLIENT_KEY,
        errp);
    if (!ifiles && *errp) {
        return -1;
    }

    if (ifiles) {
        files->identities = g_renew(QCryptoTLSCredsX509IdentFiles *,
                                    files->identities,
                                    files->nidentities + 1);
        files->identities[files->nidentities++] = ifiles;
    }

    for (i = 0; i < QCRYPTO_TLS_CREDS_X509_IDENTITY_MAX; i++) {
        g_autofree char *cert = g_strdup_printf(
            isServer ?
            QCRYPTO_TLS_CREDS_X509_SERVER_CERT_N :
            QCRYPTO_TLS_CREDS_X509_CLIENT_CERT_N, i);
        g_autofree char *key = g_strdup_printf(
            isServer ?
            QCRYPTO_TLS_CREDS_X509_SERVER_KEY_N :
            QCRYPTO_TLS_CREDS_X509_CLIENT_KEY_N, i);

        ifiles = qcrypto_tls_creds_x509_load_identity(creds, box,
                                                      cert, key, errp);
        if (!ifiles && *errp) {
            return -1;
        }
        if (!ifiles) {
            break;
        }

        files->identities = g_renew(QCryptoTLSCredsX509IdentFiles *,
                                    files->identities,
                                    files->nidentities + 1);
        files->identities[files->nidentities++] = ifiles;
    }

    if (files->nidentities == 0 && isServer) {
        g_autofree char *certpath = qcrypto_tls_creds_build_path(
            &creds->parent_obj, QCRYPTO_TLS_CREDS_X509_SERVER_CERT);
        g_autofree char *keypath = qcrypto_tls_creds_build_path(
            &creds->parent_obj, QCRYPTO_TLS_CREDS_X509_SERVER_KEY);
        error_setg(errp, "Missing server cert '%s' & key '%s'",
                   certpath, keypath);
        return -1;
    }

    return 0;
}


static int
qcrypto_tls_creds_x509_load(QCryptoTLSCredsX509 *creds,
                            Error **errp)
{
    g_autoptr(QCryptoTLSCredsBox) box = NULL;
    g_autoptr(QCryptoTLSCredsX509Files) files = NULL;
    g_autofree char *cacrl = NULL;
    g_autofree char *dhparams = NULL;
    bool isServer = (creds->parent_obj.endpoint ==
                     QCRYPTO_TLS_CREDS_ENDPOINT_SERVER);
    int ret;

    if (!creds->parent_obj.dir) {
        error_setg(errp, "Missing 'dir' property value");
        return -1;
    }

    trace_qcrypto_tls_creds_x509_load(creds, creds->parent_obj.dir);

    if (creds->parent_obj.endpoint == QCRYPTO_TLS_CREDS_ENDPOINT_SERVER) {
        box = qcrypto_tls_creds_box_new_server(GNUTLS_CRD_CERTIFICATE);
    } else {
        box = qcrypto_tls_creds_box_new_client(GNUTLS_CRD_CERTIFICATE);
    }

    ret = gnutls_certificate_allocate_credentials(&box->data.cert);
    if (ret < 0) {
        error_setg(errp, "Cannot allocate credentials: '%s'",
                   gnutls_strerror(ret));
        return -1;
    }

    files = qcrypto_tls_creds_x509_files_new();

    if (qcrypto_tls_creds_x509_load_ca(creds, box, files, isServer, errp) < 0) {
        return -1;
    }

    if (qcrypto_tls_creds_x509_load_identities(creds, box, files,
                                               isServer, errp) < 0) {
        return -1;
    }

    if (isServer) {
        if (qcrypto_tls_creds_get_path(&creds->parent_obj,
                                       QCRYPTO_TLS_CREDS_X509_CA_CRL,
                                       false, &cacrl, errp) < 0 ||
            qcrypto_tls_creds_get_path(&creds->parent_obj,
                                       QCRYPTO_TLS_CREDS_DH_PARAMS,
                                       false, &dhparams, errp) < 0) {
            return -1;
        }
    }

    if (creds->sanityCheck &&
        qcrypto_tls_creds_x509_sanity_check(creds, files, isServer, errp) < 0) {
        return -1;
    }

    if (cacrl != NULL) {
        ret = gnutls_certificate_set_x509_crl_file(box->data.cert,
                                                   cacrl,
                                                   GNUTLS_X509_FMT_PEM);
        if (ret < 0) {
            error_setg(errp, "Cannot load CRL '%s': %s",
                       cacrl, gnutls_strerror(ret));
            return -1;
        }
    }

    if (isServer) {
        if (qcrypto_tls_creds_get_dh_params_file(&creds->parent_obj, dhparams,
                                                 &box->dh_params,
                                                 errp) < 0) {
            return -1;
        }
        if (box->dh_params) {
            gnutls_certificate_set_dh_params(box->data.cert, box->dh_params);
        }
    }
    creds->parent_obj.box = g_steal_pointer(&box);

    return 0;
}


#else /* ! CONFIG_GNUTLS */


static void
qcrypto_tls_creds_x509_load(QCryptoTLSCredsX509 *creds G_GNUC_UNUSED,
                            Error **errp)
{
    error_setg(errp, "TLS credentials support requires GNUTLS");
}


#endif /* ! CONFIG_GNUTLS */


static void
qcrypto_tls_creds_x509_complete(UserCreatable *uc, Error **errp)
{
    QCryptoTLSCredsX509 *creds = QCRYPTO_TLS_CREDS_X509(uc);

    qcrypto_tls_creds_x509_load(creds, errp);
}


static void
qcrypto_tls_creds_x509_prop_set_sanity(Object *obj,
                                       bool value,
                                       Error **errp G_GNUC_UNUSED)
{
    QCryptoTLSCredsX509 *creds = QCRYPTO_TLS_CREDS_X509(obj);

    creds->sanityCheck = value;
}


static void
qcrypto_tls_creds_x509_prop_set_passwordid(Object *obj,
                                           const char *value,
                                           Error **errp G_GNUC_UNUSED)
{
    QCryptoTLSCredsX509 *creds = QCRYPTO_TLS_CREDS_X509(obj);

    creds->passwordid = g_strdup(value);
}


static char *
qcrypto_tls_creds_x509_prop_get_passwordid(Object *obj,
                                           Error **errp G_GNUC_UNUSED)
{
    QCryptoTLSCredsX509 *creds = QCRYPTO_TLS_CREDS_X509(obj);

    return g_strdup(creds->passwordid);
}


static bool
qcrypto_tls_creds_x509_prop_get_sanity(Object *obj,
                                       Error **errp G_GNUC_UNUSED)
{
    QCryptoTLSCredsX509 *creds = QCRYPTO_TLS_CREDS_X509(obj);

    return creds->sanityCheck;
}


#ifdef CONFIG_GNUTLS


static bool
qcrypto_tls_creds_x509_reload(QCryptoTLSCreds *creds, Error **errp)
{
    QCryptoTLSCredsX509 *x509_creds = QCRYPTO_TLS_CREDS_X509(creds);
    Error *local_err = NULL;
    QCryptoTLSCredsBox *creds_box = creds->box;

    creds->box = NULL;
    qcrypto_tls_creds_x509_load(x509_creds, &local_err);
    if (local_err) {
        creds->box = creds_box;
        error_propagate(errp, local_err);
        return false;
    }

    qcrypto_tls_creds_box_unref(creds_box);
    return true;
}


#else /* ! CONFIG_GNUTLS */


static bool
qcrypto_tls_creds_x509_reload(QCryptoTLSCreds *creds, Error **errp)
{
    return false;
}


#endif /* ! CONFIG_GNUTLS */


static void
qcrypto_tls_creds_x509_init(Object *obj)
{
    QCryptoTLSCredsX509 *creds = QCRYPTO_TLS_CREDS_X509(obj);

    creds->sanityCheck = true;
}


static void
qcrypto_tls_creds_x509_finalize(Object *obj)
{
    QCryptoTLSCredsX509 *creds = QCRYPTO_TLS_CREDS_X509(obj);

    g_free(creds->passwordid);
}


static void
qcrypto_tls_creds_x509_class_init(ObjectClass *oc, const void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);
    QCryptoTLSCredsClass *ctcc = QCRYPTO_TLS_CREDS_CLASS(oc);

    ctcc->reload = qcrypto_tls_creds_x509_reload;

    ucc->complete = qcrypto_tls_creds_x509_complete;

    object_class_property_add_bool(oc, "sanity-check",
                                   qcrypto_tls_creds_x509_prop_get_sanity,
                                   qcrypto_tls_creds_x509_prop_set_sanity);
    object_class_property_add_str(oc, "passwordid",
                                  qcrypto_tls_creds_x509_prop_get_passwordid,
                                  qcrypto_tls_creds_x509_prop_set_passwordid);
}


static const TypeInfo qcrypto_tls_creds_x509_info = {
    .parent = TYPE_QCRYPTO_TLS_CREDS,
    .name = TYPE_QCRYPTO_TLS_CREDS_X509,
    .instance_size = sizeof(QCryptoTLSCredsX509),
    .instance_init = qcrypto_tls_creds_x509_init,
    .instance_finalize = qcrypto_tls_creds_x509_finalize,
    .class_size = sizeof(QCryptoTLSCredsX509Class),
    .class_init = qcrypto_tls_creds_x509_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};


static void
qcrypto_tls_creds_x509_register_types(void)
{
    type_register_static(&qcrypto_tls_creds_x509_info);
}


type_init(qcrypto_tls_creds_x509_register_types);
