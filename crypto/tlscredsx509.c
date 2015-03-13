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

#include "crypto/tlscredsx509.h"
#include "crypto/tlscredspriv.h"
#include "qom/object_interfaces.h"
#include "trace.h"


#ifdef CONFIG_GNUTLS


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
        ret = gnutls_certificate_set_x509_key_file(creds->data,
                                                   cert, key,
                                                   GNUTLS_X509_FMT_PEM);
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
qcrypto_tls_creds_x509_complete(UserCreatable *uc, Error **errp)
{
    object_property_set_bool(OBJECT(uc), true, "loaded", errp);
}


static void
qcrypto_tls_creds_x509_init(Object *obj)
{
    object_property_add_bool(obj, "loaded",
                             qcrypto_tls_creds_x509_prop_get_loaded,
                             qcrypto_tls_creds_x509_prop_set_loaded,
                             NULL);
}


static void
qcrypto_tls_creds_x509_finalize(Object *obj)
{
    QCryptoTLSCredsX509 *creds = QCRYPTO_TLS_CREDS_X509(obj);

    qcrypto_tls_creds_x509_unload(creds);
}


static void
qcrypto_tls_creds_x509_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = qcrypto_tls_creds_x509_complete;
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
