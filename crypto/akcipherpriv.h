/*
 * QEMU Crypto asymmetric algorithms
 *
 * Copyright (c) 2022 Bytedance
 * Author: zhenwei pi <pizhenwei@bytedance.com>
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

#ifndef QCRYPTO_AKCIPHERPRIV_H
#define QCRYPTO_AKCIPHERPRIV_H

#include "qapi/qapi-types-crypto.h"

typedef struct QCryptoAkCipherDriver QCryptoAkCipherDriver;

struct QCryptoAkCipher {
    QCryptoAkCipherAlgo alg;
    QCryptoAkCipherKeyType type;
    int max_plaintext_len;
    int max_ciphertext_len;
    int max_signature_len;
    int max_dgst_len;
    QCryptoAkCipherDriver *driver;
};

struct QCryptoAkCipherDriver {
    int (*encrypt)(QCryptoAkCipher *akcipher,
                   const void *in, size_t in_len,
                   void *out, size_t out_len, Error **errp);
    int (*decrypt)(QCryptoAkCipher *akcipher,
                   const void *out, size_t out_len,
                   void *in, size_t in_len, Error **errp);
    int (*sign)(QCryptoAkCipher *akcipher,
                const void *in, size_t in_len,
                void *out, size_t out_len, Error **errp);
    int (*verify)(QCryptoAkCipher *akcipher,
                  const void *in, size_t in_len,
                  const void *in2, size_t in2_len, Error **errp);
    void (*free)(QCryptoAkCipher *akcipher);
};

#endif /* QCRYPTO_AKCIPHER_H */
