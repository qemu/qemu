/*
 * QEMU Crypto cipher driver supports
 *
 * Copyright (c) 2017 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Longpeng(Mike) <longpeng2@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 *
 */

#ifndef QCRYPTO_CIPHERPRIV_H
#define QCRYPTO_CIPHERPRIV_H

#include "qapi/qapi-types-crypto.h"

struct QCryptoCipherDriver {
    int (*cipher_encrypt)(QCryptoCipher *cipher,
                          const void *in,
                          void *out,
                          size_t len,
                          Error **errp);

    int (*cipher_decrypt)(QCryptoCipher *cipher,
                          const void *in,
                          void *out,
                          size_t len,
                          Error **errp);

    int (*cipher_setiv)(QCryptoCipher *cipher,
                        const uint8_t *iv, size_t niv,
                        Error **errp);

    void (*cipher_free)(QCryptoCipher *cipher);
};

#ifdef CONFIG_AF_ALG

#include "afalgpriv.h"

extern QCryptoCipher *
qcrypto_afalg_cipher_ctx_new(QCryptoCipherAlgorithm alg,
                             QCryptoCipherMode mode,
                             const uint8_t *key,
                             size_t nkey, Error **errp);

#endif

#endif
