/*
 * QEMU Crypto af_alg support
 *
 * Copyright (c) 2017 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Longpeng(Mike) <longpeng2@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef QCRYPTO_AFALGPRIV_H
#define QCRYPTO_AFALGPRIV_H

#include <linux/if_alg.h>
#include "crypto/cipher.h"

#define SALG_TYPE_LEN_MAX 14
#define SALG_NAME_LEN_MAX 64

#ifndef SOL_ALG
#define SOL_ALG 279
#endif

#define AFALG_TYPE_CIPHER "skcipher"
#define AFALG_TYPE_HASH "hash"

#define ALG_OPTYPE_LEN 4
#define ALG_MSGIV_LEN(len) (sizeof(struct af_alg_iv) + (len))

typedef struct QCryptoAFAlgo QCryptoAFAlgo;

struct QCryptoAFAlgo {
    QCryptoCipher base;

    int tfmfd;
    int opfd;
    struct msghdr *msg;
    struct cmsghdr *cmsg;
};

/**
 * qcrypto_afalg_comm_alloc:
 * @type: the type of crypto operation
 * @name: the name of crypto operation
 *
 * Allocate a QCryptoAFAlgo object and bind itself to
 * a AF_ALG socket.
 *
 * Returns:
 *  a new QCryptoAFAlgo object, or NULL in error.
 */
QCryptoAFAlgo *
qcrypto_afalg_comm_alloc(const char *type, const char *name,
                         Error **errp);

/**
 * afalg_comm_free:
 * @afalg: the QCryptoAFAlgo object
 *
 * Free the @afalg.
 */
void qcrypto_afalg_comm_free(QCryptoAFAlgo *afalg);

#endif
