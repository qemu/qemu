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
#include "crypto/secret_common.h"
#include "crypto/cipher.h"
#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "qemu/base64.h"
#include "qemu/module.h"
#include "trace.h"


static void qcrypto_secret_decrypt(QCryptoSecretCommon *secret,
                                   const uint8_t *input,
                                   size_t inputlen,
                                   uint8_t **output,
                                   size_t *outputlen,
                                   Error **errp)
{
    g_autofree uint8_t *iv = NULL;
    g_autofree uint8_t *key = NULL;
    g_autofree uint8_t *ciphertext = NULL;
    size_t keylen, ciphertextlen, ivlen;
    g_autoptr(QCryptoCipher) aes = NULL;
    g_autofree uint8_t *plaintext = NULL;

    *output = NULL;
    *outputlen = 0;

    if (qcrypto_secret_lookup(secret->keyid,
                              &key, &keylen,
                              errp) < 0) {
        return;
    }

    if (keylen != 32) {
        error_setg(errp, "Key should be 32 bytes in length");
        return;
    }

    if (!secret->iv) {
        error_setg(errp, "IV is required to decrypt secret");
        return;
    }

    iv = qbase64_decode(secret->iv, -1, &ivlen, errp);
    if (!iv) {
        return;
    }
    if (ivlen != 16) {
        error_setg(errp, "IV should be 16 bytes in length not %zu",
                   ivlen);
        return;
    }

    aes = qcrypto_cipher_new(QCRYPTO_CIPHER_ALGO_AES_256,
                             QCRYPTO_CIPHER_MODE_CBC,
                             key, keylen,
                             errp);
    if (!aes) {
        return;
    }

    if (qcrypto_cipher_setiv(aes, iv, ivlen, errp) < 0) {
        return;
    }

    if (secret->format == QCRYPTO_SECRET_FORMAT_BASE64) {
        ciphertext = qbase64_decode((const gchar *)input,
                                    inputlen,
                                    &ciphertextlen,
                                    errp);
        if (!ciphertext) {
            return;
        }
        plaintext = g_new0(uint8_t, ciphertextlen + 1);
    } else {
        ciphertextlen = inputlen;
        plaintext = g_new0(uint8_t, inputlen + 1);
    }
    if (qcrypto_cipher_decrypt(aes,
                               ciphertext ? ciphertext : input,
                               plaintext,
                               ciphertextlen,
                               errp) < 0) {
        return;
    }

    if (plaintext[ciphertextlen - 1] > 16 ||
        plaintext[ciphertextlen - 1] > ciphertextlen) {
        error_setg(errp, "Incorrect number of padding bytes (%d) "
                   "found on decrypted data",
                   (int)plaintext[ciphertextlen - 1]);
        return;
    }

    /*
     *  Even though plaintext may contain arbitrary NUL
     * ensure it is explicitly NUL terminated.
     */
    ciphertextlen -= plaintext[ciphertextlen - 1];
    plaintext[ciphertextlen] = '\0';

    *output = g_steal_pointer(&plaintext);
    *outputlen = ciphertextlen;
}


static void qcrypto_secret_decode(const uint8_t *input,
                                  size_t inputlen,
                                  uint8_t **output,
                                  size_t *outputlen,
                                  Error **errp)
{
    *output = qbase64_decode((const gchar *)input,
                             inputlen,
                             outputlen,
                             errp);
}


static void
qcrypto_secret_complete(UserCreatable *uc, Error **errp)
{
    QCryptoSecretCommon *secret = QCRYPTO_SECRET_COMMON(uc);
    QCryptoSecretCommonClass *sec_class
                                = QCRYPTO_SECRET_COMMON_GET_CLASS(uc);

    Error *local_err = NULL;
    uint8_t *input = NULL;
    size_t inputlen = 0;
    uint8_t *output = NULL;
    size_t outputlen = 0;

    if (sec_class->load_data) {
        sec_class->load_data(secret, &input, &inputlen, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    } else {
        error_setg(errp, "%s provides no 'load_data' method'",
                         object_get_typename(OBJECT(uc)));
        return;
    }

    if (secret->keyid) {
        qcrypto_secret_decrypt(secret, input, inputlen,
                               &output, &outputlen, &local_err);
        g_free(input);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
        input = output;
        inputlen = outputlen;
    } else {
        if (secret->format == QCRYPTO_SECRET_FORMAT_BASE64) {
            qcrypto_secret_decode(input, inputlen,
                                  &output, &outputlen, &local_err);
            g_free(input);
            if (local_err) {
                error_propagate(errp, local_err);
                return;
            }
            input = output;
            inputlen = outputlen;
        }
    }

    secret->rawdata = input;
    secret->rawlen = inputlen;
}


static void
qcrypto_secret_prop_set_format(Object *obj,
                               int value,
                               Error **errp G_GNUC_UNUSED)
{
    QCryptoSecretCommon *creds = QCRYPTO_SECRET_COMMON(obj);
    creds->format = value;
}


static int
qcrypto_secret_prop_get_format(Object *obj,
                               Error **errp G_GNUC_UNUSED)
{
    QCryptoSecretCommon *creds = QCRYPTO_SECRET_COMMON(obj);
    return creds->format;
}


static void
qcrypto_secret_prop_set_iv(Object *obj,
                           const char *value,
                           Error **errp)
{
    QCryptoSecretCommon *secret = QCRYPTO_SECRET_COMMON(obj);

    g_free(secret->iv);
    secret->iv = g_strdup(value);
}


static char *
qcrypto_secret_prop_get_iv(Object *obj,
                           Error **errp)
{
    QCryptoSecretCommon *secret = QCRYPTO_SECRET_COMMON(obj);
    return g_strdup(secret->iv);
}


static void
qcrypto_secret_prop_set_keyid(Object *obj,
                              const char *value,
                              Error **errp)
{
    QCryptoSecretCommon *secret = QCRYPTO_SECRET_COMMON(obj);

    g_free(secret->keyid);
    secret->keyid = g_strdup(value);
}


static char *
qcrypto_secret_prop_get_keyid(Object *obj,
                              Error **errp)
{
    QCryptoSecretCommon *secret = QCRYPTO_SECRET_COMMON(obj);
    return g_strdup(secret->keyid);
}


static void
qcrypto_secret_finalize(Object *obj)
{
    QCryptoSecretCommon *secret = QCRYPTO_SECRET_COMMON(obj);

    g_free(secret->iv);
    g_free(secret->keyid);
    g_free(secret->rawdata);
}

static void
qcrypto_secret_class_init(ObjectClass *oc, const void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = qcrypto_secret_complete;

    object_class_property_add_enum(oc, "format",
                                   "QCryptoSecretFormat",
                                   &QCryptoSecretFormat_lookup,
                                   qcrypto_secret_prop_get_format,
                                   qcrypto_secret_prop_set_format);
    object_class_property_add_str(oc, "keyid",
                                  qcrypto_secret_prop_get_keyid,
                                  qcrypto_secret_prop_set_keyid);
    object_class_property_add_str(oc, "iv",
                                  qcrypto_secret_prop_get_iv,
                                  qcrypto_secret_prop_set_iv);
}


int qcrypto_secret_lookup(const char *secretid,
                          uint8_t **data,
                          size_t *datalen,
                          Error **errp)
{
    Object *obj;
    QCryptoSecretCommon *secret;

    obj = object_resolve_path_component(
        object_get_objects_root(), secretid);
    if (!obj) {
        error_setg(errp, "No secret with id '%s'", secretid);
        return -1;
    }

    secret = (QCryptoSecretCommon *)
        object_dynamic_cast(obj,
                            TYPE_QCRYPTO_SECRET_COMMON);
    if (!secret) {
        error_setg(errp, "Object with id '%s' is not a secret",
                   secretid);
        return -1;
    }

    if (!secret->rawdata) {
        error_setg(errp, "Secret with id '%s' has no data",
                   secretid);
        return -1;
    }

    *data = g_new0(uint8_t, secret->rawlen + 1);
    memcpy(*data, secret->rawdata, secret->rawlen);
    (*data)[secret->rawlen] = '\0';
    *datalen = secret->rawlen;

    return 0;
}


char *qcrypto_secret_lookup_as_utf8(const char *secretid,
                                    Error **errp)
{
    uint8_t *data;
    size_t datalen;

    if (qcrypto_secret_lookup(secretid,
                              &data,
                              &datalen,
                              errp) < 0) {
        return NULL;
    }

    if (!g_utf8_validate((const gchar *)data, datalen, NULL)) {
        error_setg(errp,
                   "Data from secret %s is not valid UTF-8",
                   secretid);
        g_free(data);
        return NULL;
    }

    return (char *)data;
}


char *qcrypto_secret_lookup_as_base64(const char *secretid,
                                      Error **errp)
{
    uint8_t *data;
    size_t datalen;
    char *ret;

    if (qcrypto_secret_lookup(secretid,
                              &data,
                              &datalen,
                              errp) < 0) {
        return NULL;
    }

    ret = g_base64_encode(data, datalen);
    g_free(data);
    return ret;
}


static const TypeInfo qcrypto_secret_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_QCRYPTO_SECRET_COMMON,
    .instance_size = sizeof(QCryptoSecretCommon),
    .instance_finalize = qcrypto_secret_finalize,
    .class_size = sizeof(QCryptoSecretCommonClass),
    .class_init = qcrypto_secret_class_init,
    .abstract = true,
    .interfaces = (const InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};


static void
qcrypto_secret_register_types(void)
{
    type_register_static(&qcrypto_secret_info);
}


type_init(qcrypto_secret_register_types);
