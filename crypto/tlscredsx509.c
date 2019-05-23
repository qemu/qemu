/*
 * QEMU crypto TLS x509 credential support
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

#include "qemu/osdep.h"
#include "crypto/tlscredsx509.h"
#include "tlscredspriv.h"
#include "crypto/secret.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qom/object_interfaces.h"
#include "trace.h"


#ifdef CONFIG_GNUTLS

#include <gnutls/x509.h>


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
                GNUTLS_KEY_DIGITAL_SIGNATURE|GNUTLS_KEY_KEY_ENCIPHERMENT;
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
        if (!(usage & GNUTLS_KEY_KEY_ENCIPHERMENT)) {
            if (critical) {
                error_setg(errp,
                           "Certificate %s usage does not permit key "
                           "encipherment", certFile);
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
qcrypto_tls_creds_check_cert_pair(gnutls_x509_crt_t cert,
                                  const char *certFile,
                                  gnutls_x509_crt_t *cacerts,
                                  size_t ncacerts,
                                  const char *cacertFile,
                                  bool isServer,
                                  Error **errp)
{
    unsigned int status;

    if (gnutls_x509_crt_list_verify(&cert, 1,
                                    cacerts, ncacerts,
                                    NULL, 0,
                                    0, &status) < 0) {
        error_setg(errp, isServer ?
                   "Unable to verify server certificate %s against "
                   "CA certificate %s" :
                   "Unable to verify client certificate %s against "
                   "CA certificate %s",
                   certFile, cacertFile);
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

#ifndef GNUTLS_1_0_COMPAT
        if (status & GNUTLS_CERT_INSECURE_ALGORITHM) {
            reason = "The certificate uses an insecure algorithm";
        }
#endif

        error_setg(errp,
                   "Our own certificate %s failed validation against %s: %s",
                   certFile, cacertFile, reason);
        return -1;
    }

    return 0;
}


static gnutls_x509_crt_t
qcrypto_tls_creds_load_cert(QCryptoTLSCredsX509 *creds,
                            const char *certFile,
                            bool isServer,
                            Error **errp)
{
    gnutls_datum_t data;
    gnutls_x509_crt_t cert = NULL;
    char *buf = NULL;
    gsize buflen;
    GError *gerr;
    int ret = -1;
    int err;

    trace_qcrypto_tls_creds_x509_load_cert(creds, isServer, certFile);

    err = gnutls_x509_crt_init(&cert);
    if (err < 0) {
        error_setg(errp, "Unable to initialize certificate: %s",
                   gnutls_strerror(err));
        goto cleanup;
    }

    if (!g_file_get_contents(certFile, &buf, &buflen, &gerr)) {
        error_setg(errp, "Cannot load CA cert list %s: %s",
                   certFile, gerr->message);
        g_error_free(gerr);
        goto cleanup;
    }

    data.data = (unsigned char *)buf;
    data.size = strlen(buf);

    err = gnutls_x509_crt_import(cert, &data, GNUTLS_X509_FMT_PEM);
    if (err < 0) {
        error_setg(errp, isServer ?
                   "Unable to import server certificate %s: %s" :
                   "Unable to import client certificate %s: %s",
                   certFile,
                   gnutls_strerror(err));
        goto cleanup;
    }

    ret = 0;

 cleanup:
    if (ret != 0) {
        gnutls_x509_crt_deinit(cert);
        cert = NULL;
    }
    g_free(buf);
    return cert;
}


static int
qcrypto_tls_creds_load_ca_cert_list(QCryptoTLSCredsX509 *creds,
                                    const char *certFile,
                                    gnutls_x509_crt_t *certs,
                                    unsigned int certMax,
                                    size_t *ncerts,
                                    Error **errp)
{
    gnutls_datum_t data;
    char *buf = NULL;
    gsize buflen;
    int ret = -1;
    GError *gerr = NULL;

    *ncerts = 0;
    trace_qcrypto_tls_creds_x509_load_cert_list(creds, certFile);

    if (!g_file_get_contents(certFile, &buf, &buflen, &gerr)) {
        error_setg(errp, "Cannot load CA cert list %s: %s",
                   certFile, gerr->message);
        g_error_free(gerr);
        goto cleanup;
    }

    data.data = (unsigned char *)buf;
    data.size = strlen(buf);

    if (gnutls_x509_crt_list_import(certs, &certMax, &data,
                                    GNUTLS_X509_FMT_PEM, 0) < 0) {
        error_setg(errp,
                   "Unable to import CA certificate list %s",
                   certFile);
        goto cleanup;
    }
    *ncerts = certMax;

    ret = 0;

 cleanup:
    g_free(buf);
    return ret;
}


#define MAX_CERTS 16
static int
qcrypto_tls_creds_x509_sanity_check(QCryptoTLSCredsX509 *creds,
                                    bool isServer,
                                    const char *cacertFile,
                                    const char *certFile,
                                    Error **errp)
{
    gnutls_x509_crt_t cert = NULL;
    gnutls_x509_crt_t cacerts[MAX_CERTS];
    size_t ncacerts = 0;
    size_t i;
    int ret = -1;

    memset(cacerts, 0, sizeof(cacerts));
    if (certFile &&
        access(certFile, R_OK) == 0) {
        cert = qcrypto_tls_creds_load_cert(creds,
                                           certFile, isServer,
                                           errp);
        if (!cert) {
            goto cleanup;
        }
    }
    if (access(cacertFile, R_OK) == 0) {
        if (qcrypto_tls_creds_load_ca_cert_list(creds,
                                                cacertFile, cacerts,
                                                MAX_CERTS, &ncacerts,
                                                errp) < 0) {
            goto cleanup;
        }
    }

    if (cert &&
        qcrypto_tls_creds_check_cert(creds,
                                     cert, certFile, isServer,
                                     false, errp) < 0) {
        goto cleanup;
    }

    for (i = 0; i < ncacerts; i++) {
        if (qcrypto_tls_creds_check_cert(creds,
                                         cacerts[i], cacertFile,
                                         isServer, true, errp) < 0) {
            goto cleanup;
        }
    }

    if (cert && ncacerts &&
        qcrypto_tls_creds_check_cert_pair(cert, certFile, cacerts,
                                          ncacerts, cacertFile,
                                          isServer, errp) < 0) {
        goto cleanup;
    }

    ret = 0;

 cleanup:
    if (cert) {
        gnutls_x509_crt_deinit(cert);
    }
    for (i = 0; i < ncacerts; i++) {
        gnutls_x509_crt_deinit(cacerts[i]);
    }
    return ret;
}


static int
qcrypto_tls_creds_x509_load(QCryptoTLSCredsX509 *creds,
                            Error **errp)
{
    char *cacert = NULL, *cacrl = NULL, *cert = NULL,
        *key = NULL, *dhparams = NULL;
    int ret;
    int rv = -1;

    trace_qcrypto_tls_creds_x509_load(creds,
            creds->parent_obj.dir ? creds->parent_obj.dir : "<nodir>");

    if (creds->parent_obj.endpoint == QCRYPTO_TLS_CREDS_ENDPOINT_SERVER) {
        if (qcrypto_tls_creds_get_path(&creds->parent_obj,
                                       QCRYPTO_TLS_CREDS_X509_CA_CERT,
                                       true, &cacert, errp) < 0 ||
            qcrypto_tls_creds_get_path(&creds->parent_obj,
                                       QCRYPTO_TLS_CREDS_X509_CA_CRL,
                                       false, &cacrl, errp) < 0 ||
            qcrypto_tls_creds_get_path(&creds->parent_obj,
                                       QCRYPTO_TLS_CREDS_X509_SERVER_CERT,
                                       true, &cert, errp) < 0 ||
            qcrypto_tls_creds_get_path(&creds->parent_obj,
                                       QCRYPTO_TLS_CREDS_X509_SERVER_KEY,
                                       true, &key, errp) < 0 ||
            qcrypto_tls_creds_get_path(&creds->parent_obj,
                                       QCRYPTO_TLS_CREDS_DH_PARAMS,
                                       false, &dhparams, errp) < 0) {
            goto cleanup;
        }
    } else {
        if (qcrypto_tls_creds_get_path(&creds->parent_obj,
                                       QCRYPTO_TLS_CREDS_X509_CA_CERT,
                                       true, &cacert, errp) < 0 ||
            qcrypto_tls_creds_get_path(&creds->parent_obj,
                                       QCRYPTO_TLS_CREDS_X509_CLIENT_CERT,
                                       false, &cert, errp) < 0 ||
            qcrypto_tls_creds_get_path(&creds->parent_obj,
                                       QCRYPTO_TLS_CREDS_X509_CLIENT_KEY,
                                       false, &key, errp) < 0) {
            goto cleanup;
        }
    }

    if (creds->sanityCheck &&
        qcrypto_tls_creds_x509_sanity_check(creds,
            creds->parent_obj.endpoint == QCRYPTO_TLS_CREDS_ENDPOINT_SERVER,
            cacert, cert, errp) < 0) {
        goto cleanup;
    }

    ret = gnutls_certificate_allocate_credentials(&creds->data);
    if (ret < 0) {
        error_setg(errp, "Cannot allocate credentials: '%s'",
                   gnutls_strerror(ret));
        goto cleanup;
    }

    ret = gnutls_certificate_set_x509_trust_file(creds->data,
                                                 cacert,
                                                 GNUTLS_X509_FMT_PEM);
    if (ret < 0) {
        error_setg(errp, "Cannot load CA certificate '%s': %s",
                   cacert, gnutls_strerror(ret));
        goto cleanup;
    }

    if (cert != NULL && key != NULL) {
        char *password = NULL;
        if (creds->passwordid) {
            password = qcrypto_secret_lookup_as_utf8(creds->passwordid,
                                                     errp);
            if (!password) {
                goto cleanup;
            }
        }
        ret = gnutls_certificate_set_x509_key_file2(creds->data,
                                                    cert, key,
                                                    GNUTLS_X509_FMT_PEM,
                                                    password,
                                                    0);
        g_free(password);
        if (ret < 0) {
            error_setg(errp, "Cannot load certificate '%s' & key '%s': %s",
                       cert, key, gnutls_strerror(ret));
            goto cleanup;
        }
    }

    if (cacrl != NULL) {
        ret = gnutls_certificate_set_x509_crl_file(creds->data,
                                                   cacrl,
                                                   GNUTLS_X509_FMT_PEM);
        if (ret < 0) {
            error_setg(errp, "Cannot load CRL '%s': %s",
                       cacrl, gnutls_strerror(ret));
            goto cleanup;
        }
    }

    if (creds->parent_obj.endpoint == QCRYPTO_TLS_CREDS_ENDPOINT_SERVER) {
        if (qcrypto_tls_creds_get_dh_params_file(&creds->parent_obj, dhparams,
                                                 &creds->parent_obj.dh_params,
                                                 errp) < 0) {
            goto cleanup;
        }
        gnutls_certificate_set_dh_params(creds->data,
                                         creds->parent_obj.dh_params);
    }

    rv = 0;
 cleanup:
    g_free(cacert);
    g_free(cacrl);
    g_free(cert);
    g_free(key);
    g_free(dhparams);
    return rv;
}


static void
qcrypto_tls_creds_x509_unload(QCryptoTLSCredsX509 *creds)
{
    if (creds->data) {
        gnutls_certificate_free_credentials(creds->data);
        creds->data = NULL;
    }
    if (creds->parent_obj.dh_params) {
        gnutls_dh_params_deinit(creds->parent_obj.dh_params);
        creds->parent_obj.dh_params = NULL;
    }
}


#else /* ! CONFIG_GNUTLS */


static void
qcrypto_tls_creds_x509_load(QCryptoTLSCredsX509 *creds G_GNUC_UNUSED,
                            Error **errp)
{
    error_setg(errp, "TLS credentials support requires GNUTLS");
}


static void
qcrypto_tls_creds_x509_unload(QCryptoTLSCredsX509 *creds G_GNUC_UNUSED)
{
    /* nada */
}


#endif /* ! CONFIG_GNUTLS */


static void
qcrypto_tls_creds_x509_prop_set_loaded(Object *obj,
                                       bool value,
                                       Error **errp)
{
    QCryptoTLSCredsX509 *creds = QCRYPTO_TLS_CREDS_X509(obj);

    if (value) {
        qcrypto_tls_creds_x509_load(creds, errp);
    } else {
        qcrypto_tls_creds_x509_unload(creds);
    }
}


#ifdef CONFIG_GNUTLS


static bool
qcrypto_tls_creds_x509_prop_get_loaded(Object *obj,
                                       Error **errp G_GNUC_UNUSED)
{
    QCryptoTLSCredsX509 *creds = QCRYPTO_TLS_CREDS_X509(obj);

    return creds->data != NULL;
}


#else /* ! CONFIG_GNUTLS */


static bool
qcrypto_tls_creds_x509_prop_get_loaded(Object *obj G_GNUC_UNUSED,
                                       Error **errp G_GNUC_UNUSED)
{
    return false;
}


#endif /* ! CONFIG_GNUTLS */


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


static void
qcrypto_tls_creds_x509_complete(UserCreatable *uc, Error **errp)
{
    object_property_set_bool(OBJECT(uc), true, "loaded", errp);
}


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
    qcrypto_tls_creds_x509_unload(creds);
}


static void
qcrypto_tls_creds_x509_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = qcrypto_tls_creds_x509_complete;

    object_class_property_add_bool(oc, "loaded",
                                   qcrypto_tls_creds_x509_prop_get_loaded,
                                   qcrypto_tls_creds_x509_prop_set_loaded,
                                   NULL);
    object_class_property_add_bool(oc, "sanity-check",
                                   qcrypto_tls_creds_x509_prop_get_sanity,
                                   qcrypto_tls_creds_x509_prop_set_sanity,
                                   NULL);
    object_class_property_add_str(oc, "passwordid",
                                  qcrypto_tls_creds_x509_prop_get_passwordid,
                                  qcrypto_tls_creds_x509_prop_set_passwordid,
                                  NULL);
}


static const TypeInfo qcrypto_tls_creds_x509_info = {
    .parent = TYPE_QCRYPTO_TLS_CREDS,
    .name = TYPE_QCRYPTO_TLS_CREDS_X509,
    .instance_size = sizeof(QCryptoTLSCredsX509),
    .instance_init = qcrypto_tls_creds_x509_init,
    .instance_finalize = qcrypto_tls_creds_x509_finalize,
    .class_size = sizeof(QCryptoTLSCredsX509Class),
    .class_init = qcrypto_tls_creds_x509_class_init,
    .interfaces = (InterfaceInfo[]) {
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
