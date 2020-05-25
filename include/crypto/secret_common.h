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

#ifndef QCRYPTO_SECRET_COMMON_H
#define QCRYPTO_SECRET_COMMON_H

#include "qapi/qapi-types-crypto.h"
#include "qom/object.h"

#define TYPE_QCRYPTO_SECRET_COMMON "secret_common"
#define QCRYPTO_SECRET_COMMON(obj) \
    OBJECT_CHECK(QCryptoSecretCommon, (obj), TYPE_QCRYPTO_SECRET_COMMON)
#define QCRYPTO_SECRET_COMMON_CLASS(class) \
    OBJECT_CLASS_CHECK(QCryptoSecretCommonClass, \
                       (class), TYPE_QCRYPTO_SECRET_COMMON)
#define QCRYPTO_SECRET_COMMON_GET_CLASS(obj) \
    OBJECT_GET_CLASS(QCryptoSecretCommonClass, \
                     (obj), TYPE_QCRYPTO_SECRET_COMMON)

typedef struct QCryptoSecretCommon QCryptoSecretCommon;
typedef struct QCryptoSecretCommonClass QCryptoSecretCommonClass;

struct QCryptoSecretCommon {
    Object parent_obj;
    uint8_t *rawdata;
    size_t rawlen;
    QCryptoSecretFormat format;
    char *keyid;
    char *iv;
};


struct QCryptoSecretCommonClass {
    ObjectClass parent_class;
    void (*load_data)(QCryptoSecretCommon *secret,
                      uint8_t **output,
                      size_t *outputlen,
                      Error **errp);
};


extern int qcrypto_secret_lookup(const char *secretid,
                                 uint8_t **data,
                                 size_t *datalen,
                                 Error **errp);
extern char *qcrypto_secret_lookup_as_utf8(const char *secretid,
                                           Error **errp);
extern char *qcrypto_secret_lookup_as_base64(const char *secretid,
                                             Error **errp);

#endif /* QCRYPTO_SECRET_COMMON_H */
