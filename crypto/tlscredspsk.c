/*
 * QEMU crypto TLS Pre-Shared Keys (PSK) support
 *
 * Copyright (c) 2018 Red Hat, Inc.
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
#include "crypto/tlscredspsk.h"
#include "tlscredspriv.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qom/object_interfaces.h"
#include "trace.h"


#ifdef CONFIG_GNUTLS

static int
lookup_key(const char *pskfile, const char *username, gnutls_datum_t *key,
           Error **errp)
{
    const size_t ulen = strlen(username);
    GError *gerr = NULL;
    char *content = NULL;
    char **lines = NULL;
    size_t clen = 0, i;
    int ret = -1;

    if (!g_file_get_contents(pskfile, &content, &clen, &gerr)) {
        error_setg(errp, "Cannot read PSK file %s: %s",
                   pskfile, gerr->message);
        g_error_free(gerr);
        return -1;
    }

    lines = g_strsplit(content, "\n", -1);
    for (i = 0; lines[i] != NULL; ++i) {
        if (strncmp(lines[i], username, ulen) == 0 && lines[i][ulen] == ':') {
            key->data = (unsigned char *) g_strdup(&lines[i][ulen + 1]);
            key->size = strlen(lines[i]) - ulen - 1;
            ret = 0;
            goto out;
        }
    }
    error_setg(errp, "Username %s not found in PSK file %s",
               username, pskfile);

 out:
    free(content);
    g_strfreev(lines);
    return ret;
}

static int
qcrypto_tls_creds_psk_load(QCryptoTLSCredsPSK *creds,
                           Error **errp)
{
    char *pskfile = NULL, *dhparams = NULL;
    const char *username;
    int ret;
    int rv = -1;
    gnutls_datum_t key = { .data = NULL };

    trace_qcrypto_tls_creds_psk_load(creds,
            creds->parent_obj.dir ? creds->parent_obj.dir : "<nodir>");

    if (creds->parent_obj.endpoint == QCRYPTO_TLS_CREDS_ENDPOINT_SERVER) {
        if (creds->username) {
            error_setg(errp, "username should not be set when endpoint=server");
            goto cleanup;
        }

        if (qcrypto_tls_creds_get_path(&creds->parent_obj,
                                       QCRYPTO_TLS_CREDS_DH_PARAMS,
                                       false, &dhparams, errp) < 0 ||
            qcrypto_tls_creds_get_path(&creds->parent_obj,
                                       QCRYPTO_TLS_CREDS_PSKFILE,
                                       true, &pskfile, errp) < 0) {
            goto cleanup;
        }

        ret = gnutls_psk_allocate_server_credentials(&creds->data.server);
        if (ret < 0) {
            error_setg(errp, "Cannot allocate credentials: %s",
                       gnutls_strerror(ret));
            goto cleanup;
        }

        if (qcrypto_tls_creds_get_dh_params_file(&creds->parent_obj, dhparams,
                                                 &creds->parent_obj.dh_params,
                                                 errp) < 0) {
            goto cleanup;
        }

        gnutls_psk_set_server_credentials_file(creds->data.server, pskfile);
        gnutls_psk_set_server_dh_params(creds->data.server,
                                        creds->parent_obj.dh_params);
    } else {
        if (qcrypto_tls_creds_get_path(&creds->parent_obj,
                                       QCRYPTO_TLS_CREDS_PSKFILE,
                                       true, &pskfile, errp) < 0) {
            goto cleanup;
        }

        if (creds->username) {
            username = creds->username;
        } else {
            username = "qemu";
        }
        if (lookup_key(pskfile, username, &key, errp) != 0) {
            goto cleanup;
        }

        ret = gnutls_psk_allocate_client_credentials(&creds->data.client);
        if (ret < 0) {
            error_setg(errp, "Cannot allocate credentials: %s",
                       gnutls_strerror(ret));
            goto cleanup;
        }

        gnutls_psk_set_client_credentials(creds->data.client,
                                          username, &key, GNUTLS_PSK_KEY_HEX);
    }

    rv = 0;
 cleanup:
    g_free(key.data);
    g_free(pskfile);
    g_free(dhparams);
    return rv;
}


static void
qcrypto_tls_creds_psk_unload(QCryptoTLSCredsPSK *creds)
{
    if (creds->parent_obj.endpoint == QCRYPTO_TLS_CREDS_ENDPOINT_CLIENT) {
        if (creds->data.client) {
            gnutls_psk_free_client_credentials(creds->data.client);
            creds->data.client = NULL;
        }
    } else {
        if (creds->data.server) {
            gnutls_psk_free_server_credentials(creds->data.server);
            creds->data.server = NULL;
        }
    }
    if (creds->parent_obj.dh_params) {
        gnutls_dh_params_deinit(creds->parent_obj.dh_params);
        creds->parent_obj.dh_params = NULL;
    }
}

#else /* ! CONFIG_GNUTLS */


static void
qcrypto_tls_creds_psk_load(QCryptoTLSCredsPSK *creds G_GNUC_UNUSED,
                           Error **errp)
{
    error_setg(errp, "TLS credentials support requires GNUTLS");
}


static void
qcrypto_tls_creds_psk_unload(QCryptoTLSCredsPSK *creds G_GNUC_UNUSED)
{
    /* nada */
}


#endif /* ! CONFIG_GNUTLS */


static void
qcrypto_tls_creds_psk_prop_set_loaded(Object *obj,
                                      bool value,
                                      Error **errp)
{
    QCryptoTLSCredsPSK *creds = QCRYPTO_TLS_CREDS_PSK(obj);

    if (value) {
        qcrypto_tls_creds_psk_load(creds, errp);
    } else {
        qcrypto_tls_creds_psk_unload(creds);
    }
}


#ifdef CONFIG_GNUTLS


static bool
qcrypto_tls_creds_psk_prop_get_loaded(Object *obj,
                                      Error **errp G_GNUC_UNUSED)
{
    QCryptoTLSCredsPSK *creds = QCRYPTO_TLS_CREDS_PSK(obj);

    if (creds->parent_obj.endpoint == QCRYPTO_TLS_CREDS_ENDPOINT_SERVER) {
        return creds->data.server != NULL;
    } else {
        return creds->data.client != NULL;
    }
}


#else /* ! CONFIG_GNUTLS */


static bool
qcrypto_tls_creds_psk_prop_get_loaded(Object *obj G_GNUC_UNUSED,
                                      Error **errp G_GNUC_UNUSED)
{
    return false;
}


#endif /* ! CONFIG_GNUTLS */


static void
qcrypto_tls_creds_psk_complete(UserCreatable *uc, Error **errp)
{
    object_property_set_bool(OBJECT(uc), true, "loaded", errp);
}


static void
qcrypto_tls_creds_psk_finalize(Object *obj)
{
    QCryptoTLSCredsPSK *creds = QCRYPTO_TLS_CREDS_PSK(obj);

    qcrypto_tls_creds_psk_unload(creds);
}

static void
qcrypto_tls_creds_psk_prop_set_username(Object *obj,
                                        const char *value,
                                        Error **errp G_GNUC_UNUSED)
{
    QCryptoTLSCredsPSK *creds = QCRYPTO_TLS_CREDS_PSK(obj);

    creds->username = g_strdup(value);
}


static char *
qcrypto_tls_creds_psk_prop_get_username(Object *obj,
                                        Error **errp G_GNUC_UNUSED)
{
    QCryptoTLSCredsPSK *creds = QCRYPTO_TLS_CREDS_PSK(obj);

    return g_strdup(creds->username);
}

static void
qcrypto_tls_creds_psk_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = qcrypto_tls_creds_psk_complete;

    object_class_property_add_bool(oc, "loaded",
                                   qcrypto_tls_creds_psk_prop_get_loaded,
                                   qcrypto_tls_creds_psk_prop_set_loaded,
                                   NULL);
    object_class_property_add_str(oc, "username",
                                  qcrypto_tls_creds_psk_prop_get_username,
                                  qcrypto_tls_creds_psk_prop_set_username,
                                  NULL);
}


static const TypeInfo qcrypto_tls_creds_psk_info = {
    .parent = TYPE_QCRYPTO_TLS_CREDS,
    .name = TYPE_QCRYPTO_TLS_CREDS_PSK,
    .instance_size = sizeof(QCryptoTLSCredsPSK),
    .instance_finalize = qcrypto_tls_creds_psk_finalize,
    .class_size = sizeof(QCryptoTLSCredsPSKClass),
    .class_init = qcrypto_tls_creds_psk_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};


static void
qcrypto_tls_creds_psk_register_types(void)
{
    type_register_static(&qcrypto_tls_creds_psk_info);
}


type_init(qcrypto_tls_creds_psk_register_types);
