/*
 * QEMU crypto secret support
 *
 * Copyright 2020 Yandex N.V.
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
#include <asm/unistd.h>
#include <linux/keyctl.h>
#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "trace.h"
#include "crypto/secret_keyring.h"


static inline
long keyctl_read(int32_t key, uint8_t *buffer, size_t buflen)
{
    return syscall(__NR_keyctl, KEYCTL_READ, key, buffer, buflen, 0);
}


static void
qcrypto_secret_keyring_load_data(QCryptoSecretCommon *sec_common,
                                 uint8_t **output,
                                 size_t *outputlen,
                                 Error **errp)
{
    QCryptoSecretKeyring *secret = QCRYPTO_SECRET_KEYRING(sec_common);
    uint8_t *buffer = NULL;
    long retcode;

    *output = NULL;
    *outputlen = 0;

    if (!secret->serial) {
        error_setg(errp, "'serial' parameter must be provided");
        return;
    }

    retcode = keyctl_read(secret->serial, NULL, 0);
    if (retcode <= 0) {
        goto keyctl_error;
    }

    buffer = g_new0(uint8_t, retcode);

    retcode = keyctl_read(secret->serial, buffer, retcode);
    if (retcode < 0) {
        g_free(buffer);
        goto keyctl_error;
    }

    *outputlen = retcode;
    *output = buffer;
    return;

keyctl_error:
    error_setg_errno(errp, errno,
                     "Unable to read serial key %08x",
                     secret->serial);
}


static void
qcrypto_secret_prop_set_key(Object *obj, Visitor *v,
                            const char *name, void *opaque,
                            Error **errp)
{
    QCryptoSecretKeyring *secret = QCRYPTO_SECRET_KEYRING(obj);
    int32_t value;
    visit_type_int32(v, name, &value, errp);
    if (!value) {
        error_setg(errp, "'serial' should not be equal to 0");
    }
    secret->serial = value;
}


static void
qcrypto_secret_prop_get_key(Object *obj, Visitor *v,
                            const char *name, void *opaque,
                            Error **errp)
{
    QCryptoSecretKeyring *secret = QCRYPTO_SECRET_KEYRING(obj);
    int32_t value = secret->serial;
    visit_type_int32(v, name, &value, errp);
}


static void
qcrypto_secret_keyring_class_init(ObjectClass *oc, void *data)
{
    QCryptoSecretCommonClass *sic = QCRYPTO_SECRET_COMMON_CLASS(oc);
    sic->load_data = qcrypto_secret_keyring_load_data;

    object_class_property_add(oc, "serial", "int32_t",
                                  qcrypto_secret_prop_get_key,
                                  qcrypto_secret_prop_set_key,
                                  NULL, NULL);
}


static const TypeInfo qcrypto_secret_info = {
    .parent = TYPE_QCRYPTO_SECRET_COMMON,
    .name = TYPE_QCRYPTO_SECRET_KEYRING,
    .instance_size = sizeof(QCryptoSecretKeyring),
    .class_init = qcrypto_secret_keyring_class_init,
};


static void
qcrypto_secret_register_types(void)
{
    type_register_static(&qcrypto_secret_info);
}


type_init(qcrypto_secret_register_types);
