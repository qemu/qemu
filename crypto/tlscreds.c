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
#include "qemu/error-report.h"
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

    if (filename != NULL) {
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
        warn_report_once("Use of an external DH parameters file '%s' is "
                         "deprecated and will be removed in a future release",
                         filename);

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
    } else {
        *dh_params = NULL;
    }

    return 0;
}


char *
qcrypto_tls_creds_build_path(QCryptoTLSCreds *creds,
                             const char *filename)
{
    return g_strdup_printf("%s/%s", creds->dir, filename);
}


int
qcrypto_tls_creds_get_path(QCryptoTLSCreds *creds,
                           const char *filename,
                           bool required,
                           char **cred,
                           Error **errp)
{
    int ret = -1;

    *cred = qcrypto_tls_creds_build_path(creds, filename);

    if (access(*cred, R_OK) < 0) {
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
qcrypto_tls_creds_class_init(ObjectClass *oc, const void *data)
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

#ifdef CONFIG_GNUTLS
    qcrypto_tls_creds_box_unref(creds->box);
#endif
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


char *qcrypto_tls_creds_get_priority(QCryptoTLSCreds *creds)
{
    QCryptoTLSCredsClass *tcc = QCRYPTO_TLS_CREDS_GET_CLASS(creds);
    const char *priorityBase =
        creds->priority ? creds->priority : CONFIG_TLS_PRIORITY;

    if (tcc->prioritySuffix) {
        return g_strdup_printf("%s:%s", priorityBase, tcc->prioritySuffix);
    } else {
        return g_strdup(priorityBase);
    }
}


bool qcrypto_tls_creds_reload(QCryptoTLSCreds *creds,
                              Error **errp)
{
    QCryptoTLSCredsClass *credscls = QCRYPTO_TLS_CREDS_GET_CLASS(creds);

    if (credscls->reload) {
        return credscls->reload(creds, errp);
    }

    error_setg(errp, "%s does not support reloading credentials",
               object_get_typename(OBJECT(creds)));
    return false;
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
