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

#ifndef QCRYPTO_SECRET_H
#define QCRYPTO_SECRET_H

#include "qapi/qapi-types-crypto.h"
#include "qom/object.h"
#include "crypto/secret_common.h"

#define TYPE_QCRYPTO_SECRET "secret"
typedef struct QCryptoSecret QCryptoSecret;
DECLARE_INSTANCE_CHECKER(QCryptoSecret, QCRYPTO_SECRET,
                         TYPE_QCRYPTO_SECRET)

typedef struct QCryptoSecretClass QCryptoSecretClass;

/**
 * QCryptoSecret:
 *
 * The QCryptoSecret object provides storage of secrets,
 * which may be user passwords, encryption keys or any
 * other kind of sensitive data that is represented as
 * a sequence of bytes.
 *
 * The sensitive data associated with the secret can
 * be provided directly via the 'data' property, or
 * indirectly via the 'file' property. In the latter
 * case there is support for file descriptor passing
 * via the usual /dev/fdset/NN syntax that QEMU uses.
 *
 * The data for a secret can be provided in two formats,
 * either as a UTF-8 string (the default), or as base64
 * encoded 8-bit binary data. The latter is appropriate
 * for raw encryption keys, while the former is appropriate
 * for user entered passwords.
 *
 * The data may be optionally encrypted with AES-256-CBC,
 * and the decryption key provided by another
 * QCryptoSecret instance identified by the 'keyid'
 * property. When passing sensitive data directly
 * via the 'data' property it is strongly recommended
 * to use the AES encryption facility to prevent the
 * sensitive data being exposed in the process listing
 * or system log files.
 *
 * Providing data directly, insecurely (suitable for
 * ad hoc developer testing only)
 *
 *  $QEMU -object secret,id=sec0,data=letmein
 *
 * Providing data indirectly:
 *
 *  # printf "letmein" > password.txt
 *  # $QEMU \
 *      -object secret,id=sec0,file=password.txt
 *
 * Using a master encryption key with data.
 *
 * The master key needs to be created as 32 secure
 * random bytes (optionally base64 encoded)
 *
 *  # openssl rand -base64 32 > key.b64
 *  # KEY=$(base64 -d key.b64 | hexdump  -v -e '/1 "%02X"')
 *
 * Each secret to be encrypted needs to have a random
 * initialization vector generated. These do not need
 * to be kept secret
 *
 *  # openssl rand -base64 16 > iv.b64
 *  # IV=$(base64 -d iv.b64 | hexdump  -v -e '/1 "%02X"')
 *
 * A secret to be defined can now be encrypted
 *
 *  # SECRET=$(printf "letmein" |
 *             openssl enc -aes-256-cbc -a -K $KEY -iv $IV)
 *
 * When launching QEMU, create a master secret pointing
 * to key.b64 and specify that to be used to decrypt
 * the user password
 *
 *  # $QEMU \
 *      -object secret,id=secmaster0,format=base64,file=key.b64 \
 *      -object secret,id=sec0,keyid=secmaster0,format=base64,\
 *          data=$SECRET,iv=$(<iv.b64)
 *
 * When encrypting, the data can still be provided via an
 * external file, in which case it is possible to use either
 * raw binary data, or base64 encoded. This example uses
 * raw format
 *
 *  # printf "letmein" |
 *       openssl enc -aes-256-cbc -K $KEY -iv $IV -o pw.aes
 *  # $QEMU \
 *      -object secret,id=secmaster0,format=base64,file=key.b64 \
 *      -object secret,id=sec0,keyid=secmaster0,\
 *          file=pw.aes,iv=$(<iv.b64)
 *
 * Note that the ciphertext can be in either raw or base64
 * format, as indicated by the 'format' parameter, but the
 * plaintext resulting from decryption is expected to always
 * be in raw format.
 */

struct QCryptoSecret {
    QCryptoSecretCommon parent_obj;
    char *data;
    char *file;
};


struct QCryptoSecretClass {
    QCryptoSecretCommonClass parent_class;
};

#endif /* QCRYPTO_SECRET_H */
