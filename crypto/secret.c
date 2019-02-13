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
#include "crypto/cipher.h"
#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "qemu/base64.h"
#include "qemu/module.h"
#include "trace.h"


static void
qcrypto_secret_load_data(QCryptoSecret *secret,
                         uint8_t **output,
                         size_t *outputlen,
                         Error **errp)
{
    char *data = NULL;
    size_t length = 0;
    GError *gerr = NULL;

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


static void qcrypto_secret_decrypt(QCryptoSecret *secret,
                                   const uint8_t *input,
                                   size_t inputlen,
                                   uint8_t **output,
                                   size_t *outputlen,
                                   Error **errp)
{
    uint8_t *key = NULL, *ciphertext = NULL, *iv = NULL;
    size_t keylen, ciphertextlen, ivlen;
    QCryptoCipher *aes = NULL;
    uint8_t *plaintext = NULL;

    *output = NULL;
    *outputlen = 0;

    if (qcrypto_secret_lookup(secret->keyid,
                              &key, &keylen,
                              errp) < 0) {
        goto cleanup;
    }

    if (keylen != 32) {
        error_setg(errp, "Key should be 32 bytes in length");
        goto cleanup;
    }

    if (!secret->iv) {
        error_setg(errp, "IV is required to decrypt secret");
        goto cleanup;
    }

    iv = qbase64_decode(secret->iv, -1, &ivlen, errp);
    if (!iv) {
        goto cleanup;
    }
    if (ivlen != 16) {
        error_setg(errp, "IV should be 16 bytes in length not %zu",
                   ivlen);
        goto cleanup;
    }

    aes = qcrypto_cipher_new(QCRYPTO_CIPHER_ALG_AES_256,
                             QCRYPTO_CIPHER_MODE_CBC,
                             key, keylen,
                             errp);
    if (!aes) {
        goto cleanup;
    }

    if (qcrypto_cipher_setiv(aes, iv, ivlen, errp) < 0) {
        goto cleanup;
    }

    if (secret->format == QCRYPTO_SECRET_FORMAT_BASE64) {
        ciphertext = qbase64_decode((const gchar*)input,
                                    inputlen,
                                    &ciphertextlen,
                                    errp);
        if (!ciphertext) {
            goto cleanup;
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
        plaintext = NULL;
        goto cleanup;
    }

    if (plaintext[ciphertextlen - 1] > 16 ||
        plaintext[ciphertextlen - 1] > ciphertextlen) {
        error_setg(errp, "Incorrect number of padding bytes (%d) "
                   "found on decrypted data",
                   (int)plaintext[ciphertextlen - 1]);
        g_free(plaintext);
        plaintext = NULL;
        goto cleanup;
    }

    /* Even though plaintext may contain arbitrary NUL
     * ensure it is explicitly NUL terminated.
     */
    ciphertextlen -= plaintext[ciphertextlen - 1];
    plaintext[ciphertextlen] = '\0';

    *output = plaintext;
    *outputlen = ciphertextlen;

 cleanup:
    g_free(ciphertext);
    g_free(iv);
    g_free(key);
    qcrypto_cipher_free(aes);
}


static void qcrypto_secret_decode(const uint8_t *input,
                                  size_t inputlen,
                                  uint8_t **output,
                                  size_t *outputlen,
                                  Error **errp)
{
    *output = qbase64_decode((const gchar*)input,
                             inputlen,
                             outputlen,
                             errp);
}


static void
qcrypto_secret_prop_set_loaded(Object *obj,
                               bool value,
                               Error **errp)
{
    QCryptoSecret *secret = QCRYPTO_SECRET(obj);

    if (value) {
        Error *local_err = NULL;
        uint8_t *input = NULL;
        size_t inputlen = 0;
        uint8_t *output = NULL;
        size_t outputlen = 0;

        qcrypto_secret_load_data(secret, &input, &inputlen, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
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
            if (secret->format != QCRYPTO_SECRET_FORMAT_RAW) {
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
    } else {
        g_free(secret->rawdata);
        secret->rawlen = 0;
    }
}


static bool
qcrypto_secret_prop_get_loaded(Object *obj,
                               Error **errp G_GNUC_UNUSED)
{
    QCryptoSecret *secret = QCRYPTO_SECRET(obj);
    return secret->data != NULL;
}


static void
qcrypto_secret_prop_set_format(Object *obj,
                               int value,
                               Error **errp G_GNUC_UNUSED)
{
    QCryptoSecret *creds = QCRYPTO_SECRET(obj);

    creds->format = value;
}


static int
qcrypto_secret_prop_get_format(Object *obj,
                               Error **errp G_GNUC_UNUSED)
{
    QCryptoSecret *creds = QCRYPTO_SECRET(obj);

    return creds->format;
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
qcrypto_secret_prop_set_iv(Object *obj,
                           const char *value,
                           Error **errp)
{
    QCryptoSecret *secret = QCRYPTO_SECRET(obj);

    g_free(secret->iv);
    secret->iv = g_strdup(value);
}


static char *
qcrypto_secret_prop_get_iv(Object *obj,
                           Error **errp)
{
    QCryptoSecret *secret = QCRYPTO_SECRET(obj);
    return g_strdup(secret->iv);
}


static void
qcrypto_secret_prop_set_keyid(Object *obj,
                              const char *value,
                              Error **errp)
{
    QCryptoSecret *secret = QCRYPTO_SECRET(obj);

    g_free(secret->keyid);
    secret->keyid = g_strdup(value);
}


static char *
qcrypto_secret_prop_get_keyid(Object *obj,
                              Error **errp)
{
    QCryptoSecret *secret = QCRYPTO_SECRET(obj);
    return g_strdup(secret->keyid);
}


static void
qcrypto_secret_complete(UserCreatable *uc, Error **errp)
{
    object_property_set_bool(OBJECT(uc), true, "loaded", errp);
}


static void
qcrypto_secret_finalize(Object *obj)
{
    QCryptoSecret *secret = QCRYPTO_SECRET(obj);

    g_free(secret->iv);
    g_free(secret->file);
    g_free(secret->keyid);
    g_free(secret->rawdata);
    g_free(secret->data);
}

static void
qcrypto_secret_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = qcrypto_secret_complete;

    object_class_property_add_bool(oc, "loaded",
                                   qcrypto_secret_prop_get_loaded,
                                   qcrypto_secret_prop_set_loaded,
                                   NULL);
    object_class_property_add_enum(oc, "format",
                                   "QCryptoSecretFormat",
                                   &QCryptoSecretFormat_lookup,
                                   qcrypto_secret_prop_get_format,
                                   qcrypto_secret_prop_set_format,
                                   NULL);
    object_class_property_add_str(oc, "data",
                                  qcrypto_secret_prop_get_data,
                                  qcrypto_secret_prop_set_data,
                                  NULL);
    object_class_property_add_str(oc, "file",
                                  qcrypto_secret_prop_get_file,
                                  qcrypto_secret_prop_set_file,
                                  NULL);
    object_class_property_add_str(oc, "keyid",
                                  qcrypto_secret_prop_get_keyid,
                                  qcrypto_secret_prop_set_keyid,
                                  NULL);
    object_class_property_add_str(oc, "iv",
                                  qcrypto_secret_prop_get_iv,
                                  qcrypto_secret_prop_set_iv,
                                  NULL);
}


int qcrypto_secret_lookup(const char *secretid,
                          uint8_t **data,
                          size_t *datalen,
                          Error **errp)
{
    Object *obj;
    QCryptoSecret *secret;

    obj = object_resolve_path_component(
        object_get_objects_root(), secretid);
    if (!obj) {
        error_setg(errp, "No secret with id '%s'", secretid);
        return -1;
    }

    secret = (QCryptoSecret *)
        object_dynamic_cast(obj,
                            TYPE_QCRYPTO_SECRET);
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

    if (!g_utf8_validate((const gchar*)data, datalen, NULL)) {
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
    .name = TYPE_QCRYPTO_SECRET,
    .instance_size = sizeof(QCryptoSecret),
    .instance_finalize = qcrypto_secret_finalize,
    .class_size = sizeof(QCryptoSecretClass),
    .class_init = qcrypto_secret_class_init,
    .interfaces = (InterfaceInfo[]) {
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
