/*
 * QEMU crypto TLS credential support
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
#include "qapi/error.h"
#include "qapi-types-crypto.h"
#include "qemu/module.h"
#include "tlscredspriv.h"
#include "trace.h"

#define DH_BITS 2048

#ifdef CONFIG_GNUTLS
int
qcrypto_tls_creds_get_dh_params_file(QCryptoTLSCreds *creds,
                                     const char *filename,
                                     gnutls_dh_params_t *dh_params,
                                     Error **errp)
{
    int ret;

    trace_qcrypto_tls_creds_load_dh(creds, filename ? filename : "<generated>");

    if (filename == NULL) {
        ret = gnutls_dh_params_init(dh_params);
        if (ret < 0) {
            error_setg(errp, "Unable to initialize DH parameters: %s",
                       gnutls_strerror(ret));
            return -1;
        }
        ret = gnutls_dh_params_generate2(*dh_params, DH_BITS);
        if (ret < 0) {
            gnutls_dh_params_deinit(*dh_params);
            *dh_params = NULL;
            error_setg(errp, "Unable to generate DH parameters: %s",
                       gnutls_strerror(ret));
            return -1;
        }
    } else {
        GError *gerr = NULL;
        gchar *contents;
        gsize len;
        gnutls_datum_t data;
        if (!g_file_get_contents(filename,
                                 &contents,
                                 &len,
                                 &gerr)) {

            error_setg(errp, "%s", gerr->message);
            g_error_free(gerr);
            return -1;
        }
        data.data = (unsigned char *)contents;
        data.size = len;
        ret = gnutls_dh_params_init(dh_params);
        if (ret < 0) {
            g_free(contents);
            error_setg(errp, "Unable to initialize DH parameters: %s",
                       gnutls_strerror(ret));
            return -1;
        }
        ret = gnutls_dh_params_import_pkcs3(*dh_params,
                                            &data,
                                            GNUTLS_X509_FMT_PEM);
        g_free(contents);
        if (ret < 0) {
            gnutls_dh_params_deinit(*dh_params);
            *dh_params = NULL;
            error_setg(errp, "Unable to load DH parameters from %s: %s",
                       filename, gnutls_strerror(ret));
            return -1;
        }
    }

    return 0;
}


int
qcrypto_tls_creds_get_path(QCryptoTLSCreds *creds,
                           const char *filename,
                           bool required,
                           char **cred,
                           Error **errp)
{
    struct stat sb;
    int ret = -1;

    if (!creds->dir) {
        if (required) {
            error_setg(errp, "Missing 'dir' property value");
            return -1;
        } else {
            return 0;
        }
    }

    *cred = g_strdup_printf("%s/%s", creds->dir, filename);

    if (stat(*cred, &sb) < 0) {
        if (errno == ENOENT && !required) {
            ret = 0;
        } else {
            error_setg_errno(errp, errno,
                             "Unable to access credentials %s",
                             *cred);
        }
        g_free(*cred);
        *cred = NULL;
        goto cleanup;
    }

    ret = 0;
 cleanup:
    trace_qcrypto_tls_creds_get_path(creds, filename,
                                     *cred ? *cred : "<none>");
    return ret;
}


#endif /* ! CONFIG_GNUTLS */


static void
qcrypto_tls_creds_prop_set_verify(Object *obj,
                                  bool value,
                                  Error **errp G_GNUC_UNUSED)
{
    QCryptoTLSCreds *creds = QCRYPTO_TLS_CREDS(obj);

    creds->verifyPeer = value;
}


static bool
qcrypto_tls_creds_prop_get_verify(Object *obj,
                                  Error **errp G_GNUC_UNUSED)
{
    QCryptoTLSCreds *creds = QCRYPTO_TLS_CREDS(obj);

    return creds->verifyPeer;
}


static void
qcrypto_tls_creds_prop_set_dir(Object *obj,
                               const char *value,
                               Error **errp G_GNUC_UNUSED)
{
    QCryptoTLSCreds *creds = QCRYPTO_TLS_CREDS(obj);

    creds->dir = g_strdup(value);
}


static char *
qcrypto_tls_creds_prop_get_dir(Object *obj,
                               Error **errp G_GNUC_UNUSED)
{
    QCryptoTLSCreds *creds = QCRYPTO_TLS_CREDS(obj);

    return g_strdup(creds->dir);
}


static void
qcrypto_tls_creds_prop_set_priority(Object *obj,
                                    const char *value,
                                    Error **errp G_GNUC_UNUSED)
{
    QCryptoTLSCreds *creds = QCRYPTO_TLS_CREDS(obj);

    creds->priority = g_strdup(value);
}


static char *
qcrypto_tls_creds_prop_get_priority(Object *obj,
                                    Error **errp G_GNUC_UNUSED)
{
    QCryptoTLSCreds *creds = QCRYPTO_TLS_CREDS(obj);

    return g_strdup(creds->priority);
}


static void
qcrypto_tls_creds_prop_set_endpoint(Object *obj,
                                    int value,
                                    Error **errp G_GNUC_UNUSED)
{
    QCryptoTLSCreds *creds = QCRYPTO_TLS_CREDS(obj);

    creds->endpoint = value;
}


static int
qcrypto_tls_creds_prop_get_endpoint(Object *obj,
                                    Error **errp G_GNUC_UNUSED)
{
    QCryptoTLSCreds *creds = QCRYPTO_TLS_CREDS(obj);

    return creds->endpoint;
}


static void
qcrypto_tls_creds_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_bool(oc, "verify-peer",
                                   qcrypto_tls_creds_prop_get_verify,
                                   qcrypto_tls_creds_prop_set_verify);
    object_class_property_add_str(oc, "dir",
                                  qcrypto_tls_creds_prop_get_dir,
                                  qcrypto_tls_creds_prop_set_dir);
    object_class_property_add_enum(oc, "endpoint",
                                   "QCryptoTLSCredsEndpoint",
                                   &QCryptoTLSCredsEndpoint_lookup,
                                   qcrypto_tls_creds_prop_get_endpoint,
                                   qcrypto_tls_creds_prop_set_endpoint);
    object_class_property_add_str(oc, "priority",
                                  qcrypto_tls_creds_prop_get_priority,
                                  qcrypto_tls_creds_prop_set_priority);
}


static void
qcrypto_tls_creds_init(Object *obj)
{
    QCryptoTLSCreds *creds = QCRYPTO_TLS_CREDS(obj);

    creds->verifyPeer = true;
}


static void
qcrypto_tls_creds_finalize(Object *obj)
{
    QCryptoTLSCreds *creds = QCRYPTO_TLS_CREDS(obj);

    g_free(creds->dir);
    g_free(creds->priority);
}

bool qcrypto_tls_creds_check_endpoint(QCryptoTLSCreds *creds,
                                      QCryptoTLSCredsEndpoint endpoint,
                                      Error **errp)
{
    if (creds->endpoint != endpoint) {
        error_setg(errp, "Expected TLS credentials for a %s endpoint",
                   QCryptoTLSCredsEndpoint_str(endpoint));
        return false;
    }
    return true;
}

static const TypeInfo qcrypto_tls_creds_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_QCRYPTO_TLS_CREDS,
    .instance_size = sizeof(QCryptoTLSCreds),
    .instance_init = qcrypto_tls_creds_init,
    .instance_finalize = qcrypto_tls_creds_finalize,
    .class_init = qcrypto_tls_creds_class_init,
    .class_size = sizeof(QCryptoTLSCredsClass),
    .abstract = true,
};


static void
qcrypto_tls_creds_register_types(void)
{
    type_register_static(&qcrypto_tls_creds_info);
}


type_init(qcrypto_tls_creds_register_types);
