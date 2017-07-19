/*
 * QEMU Crypto hash driver supports
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

#ifndef QCRYPTO_HASHPRIV_H
#define QCRYPTO_HASHPRIV_H

typedef struct QCryptoHashDriver QCryptoHashDriver;

struct QCryptoHashDriver {
    int (*hash_bytesv)(QCryptoHashAlgorithm alg,
                       const struct iovec *iov,
                       size_t niov,
                       uint8_t **result,
                       size_t *resultlen,
                       Error **errp);
};

extern QCryptoHashDriver qcrypto_hash_lib_driver;

#ifdef CONFIG_AF_ALG

#include "afalgpriv.h"

extern QCryptoHashDriver qcrypto_hash_afalg_driver;

#endif

#endif
