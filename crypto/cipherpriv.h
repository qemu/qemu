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

typedef struct QCryptoCipherDriver QCryptoCipherDriver;

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

#endif
