/*
 * QEMU Crypto hash driver supports
 *
 * Copyright (c) 2024 Seagate Technology LLC and/or its Affiliates
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

#include "crypto/hash.h"

typedef struct QCryptoHashDriver QCryptoHashDriver;

struct QCryptoHashDriver {
    QCryptoHash *(*hash_new)(QCryptoHashAlgo alg, Error **errp);
    int (*hash_update)(QCryptoHash *hash,
                       const struct iovec *iov,
                       size_t niov,
                       Error **errp);
    int (*hash_finalize)(QCryptoHash *hash,
                         uint8_t **result,
                         size_t *resultlen,
                         Error **errp);
    void (*hash_free)(QCryptoHash *hash);
};

extern QCryptoHashDriver qcrypto_hash_lib_driver;

#ifdef CONFIG_AF_ALG

#include "afalgpriv.h"

extern QCryptoHashDriver qcrypto_hash_afalg_driver;

#endif

#endif
