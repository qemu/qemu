/*
 * QEMU crypto secret support
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
#include "crypto/secret.h"
#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "qemu/module.h"
#include "trace.h"


static void
qcrypto_secret_load_data(QCryptoSecretCommon *sec_common,
                         uint8_t **output,
                         size_t *outputlen,
                         Error **errp)
{
    char *data = NULL;
    size_t length = 0;
    GError *gerr = NULL;

    QCryptoSecret *secret = QCRYPTO_SECRET(sec_common);

    *output = NULL;
    *outputlen = 0;

    if (secret->file) {
        if (secret->data) {
            error_setg(errp,
                       "'file' and 'data' are mutually exclusive");
            return;
        }
        if (!g_file_get_contents(secret->file, &data, &length, &gerr)) {
            error_setg(errp,
                       "Unable to read %s: %s",
                       secret->file, gerr->message);
            g_error_free(gerr);
            return;
        }
        *output = (uint8_t *)data;
        *outputlen = length;
    } else if (secret->data) {
        *outputlen = strlen(secret->data);
        *output = (uint8_t *)g_strdup(secret->data);
    } else {
        error_setg(errp, "Either 'file' or 'data' must be provided");
    }
}


static void
qcrypto_secret_prop_set_data(Object *obj,
                             const char *value,
                             Error **errp)
{
    QCryptoSecret *secret = QCRYPTO_SECRET(obj);

    g_free(secret->data);
    secret->data = g_strdup(value);
}


static char *
qcrypto_secret_prop_get_data(Object *obj,
                             Error **errp)
{
    QCryptoSecret *secret = QCRYPTO_SECRET(obj);
    return g_strdup(secret->data);
}


static void
qcrypto_secret_prop_set_file(Object *obj,
                             const char *value,
                             Error **errp)
{
    QCryptoSecret *secret = QCRYPTO_SECRET(obj);

    g_free(secret->file);
    secret->file = g_strdup(value);
}


static char *
qcrypto_secret_prop_get_file(Object *obj,
                             Error **errp)
{
    QCryptoSecret *secret = QCRYPTO_SECRET(obj);
    return g_strdup(secret->file);
}


static void
qcrypto_secret_finalize(Object *obj)
{
    QCryptoSecret *secret = QCRYPTO_SECRET(obj);

    g_free(secret->file);
    g_free(secret->data);
}

static void
qcrypto_secret_class_init(ObjectClass *oc, void *data)
{
    QCryptoSecretCommonClass *sic = QCRYPTO_SECRET_COMMON_CLASS(oc);
    sic->load_data = qcrypto_secret_load_data;

    object_class_property_add_str(oc, "data",
                                  qcrypto_secret_prop_get_data,
                                  qcrypto_secret_prop_set_data);
    object_class_property_add_str(oc, "file",
                                  qcrypto_secret_prop_get_file,
                                  qcrypto_secret_prop_set_file);
}


static const TypeInfo qcrypto_secret_info = {
    .parent = TYPE_QCRYPTO_SECRET_COMMON,
    .name = TYPE_QCRYPTO_SECRET,
    .instance_size = sizeof(QCryptoSecret),
    .instance_finalize = qcrypto_secret_finalize,
    .class_size = sizeof(QCryptoSecretClass),
    .class_init = qcrypto_secret_class_init,
};


static void
qcrypto_secret_register_types(void)
{
    type_register_static(&qcrypto_secret_info);
}


type_init(qcrypto_secret_register_types);
