/*
 * QEMU Crypto block IV generator
 *
 * Copyright (c) 2015-2016 Red Hat, Inc.
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

#ifndef QCRYPTO_IVGENPRIV_H
#define QCRYPTO_IVGENPRIV_H

#include "crypto/ivgen.h"

typedef struct QCryptoIVGenDriver QCryptoIVGenDriver;

struct QCryptoIVGenDriver {
    int (*init)(QCryptoIVGen *ivgen,
                const uint8_t *key, size_t nkey,
                Error **errp);
    int (*calculate)(QCryptoIVGen *ivgen,
                     uint64_t sector,
                     uint8_t *iv, size_t niv,
                     Error **errp);
    void (*cleanup)(QCryptoIVGen *ivgen);
};

struct QCryptoIVGen {
    QCryptoIVGenDriver *driver;
    void *private;

    QCryptoIVGenAlgorithm algorithm;
    QCryptoCipherAlgorithm cipher;
    QCryptoHashAlgorithm hash;
};


#endif /* QCRYPTO_IVGENPRIV_H */
