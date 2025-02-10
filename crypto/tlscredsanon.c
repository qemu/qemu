/*
 * QEMU crypto TLS anonymous credential support
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
#include "crypto/tlscredsanon.h"
#include "tlscredspriv.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qom/object_interfaces.h"
#include "trace.h"


#ifdef CONFIG_GNUTLS

#include <gnutls/gnutls.h>


static int
qcrypto_tls_creds_anon_load(QCryptoTLSCredsAnon *creds,
                            Error **errp)
{
    g_autofree char *dhparams = NULL;
    int ret;

    trace_qcrypto_tls_creds_anon_load(creds,
            creds->parent_obj.dir ? creds->parent_obj.dir : "<nodir>");

    if (creds->parent_obj.endpoint == QCRYPTO_TLS_CREDS_ENDPOINT_SERVER) {
        if (qcrypto_tls_creds_get_path(&creds->parent_obj,
                                       QCRYPTO_TLS_CREDS_DH_PARAMS,
                                       false, &dhparams, errp) < 0) {
            return -1;
        }

        ret = gnutls_anon_allocate_server_credentials(&creds->data.server);
        if (ret < 0) {
            error_setg(errp, "Cannot allocate credentials: %s",
                       gnutls_strerror(ret));
            return -1;
        }

        if (qcrypto_tls_creds_get_dh_params_file(&creds->parent_obj, dhparams,
                                                 &creds->parent_obj.dh_params,
                                                 errp) < 0) {
            return -1;
        }

        gnutls_anon_set_server_dh_params(creds->data.server,
                                         creds->parent_obj.dh_params);
    } else {
        ret = gnutls_anon_allocate_client_credentials(&creds->data.client);
        if (ret < 0) {
            error_setg(errp, "Cannot allocate credentials: %s",
                       gnutls_strerror(ret));
            return -1;
        }
    }

    return 0;
}


static void
qcrypto_tls_creds_anon_unload(QCryptoTLSCredsAnon *creds)
{
    if (creds->parent_obj.endpoint == QCRYPTO_TLS_CREDS_ENDPOINT_CLIENT) {
        if (creds->data.client) {
            gnutls_anon_free_client_credentials(creds->data.client);
            creds->data.client = NULL;
        }
    } else {
        if (creds->data.server) {
            gnutls_anon_free_server_credentials(creds->data.server);
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
qcrypto_tls_creds_anon_load(QCryptoTLSCredsAnon *creds G_GNUC_UNUSED,
                            Error **errp)
{
    error_setg(errp, "TLS credentials support requires GNUTLS");
}


static void
qcrypto_tls_creds_anon_unload(QCryptoTLSCredsAnon *creds G_GNUC_UNUSED)
{
    /* nada */
}


#endif /* ! CONFIG_GNUTLS */


static void
qcrypto_tls_creds_anon_complete(UserCreatable *uc, Error **errp)
{
    QCryptoTLSCredsAnon *creds = QCRYPTO_TLS_CREDS_ANON(uc);

    qcrypto_tls_creds_anon_load(creds, errp);
}


static void
qcrypto_tls_creds_anon_finalize(Object *obj)
{
    QCryptoTLSCredsAnon *creds = QCRYPTO_TLS_CREDS_ANON(obj);

    qcrypto_tls_creds_anon_unload(creds);
}


static void
qcrypto_tls_creds_anon_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = qcrypto_tls_creds_anon_complete;
}


static const TypeInfo qcrypto_tls_creds_anon_info = {
    .parent = TYPE_QCRYPTO_TLS_CREDS,
    .name = TYPE_QCRYPTO_TLS_CREDS_ANON,
    .instance_size = sizeof(QCryptoTLSCredsAnon),
    .instance_finalize = qcrypto_tls_creds_anon_finalize,
    .class_size = sizeof(QCryptoTLSCredsAnonClass),
    .class_init = qcrypto_tls_creds_anon_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};


static void
qcrypto_tls_creds_anon_register_types(void)
{
    type_register_static(&qcrypto_tls_creds_anon_info);
}


type_init(qcrypto_tls_creds_anon_register_types);
