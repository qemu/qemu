/*
 * QEMU Crypto block device encryption
 *
 * Copyright (c) 2015-2016 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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

#ifndef QCRYPTO_BLOCKPRIV_H
#define QCRYPTO_BLOCKPRIV_H

#include "crypto/block.h"

typedef struct QCryptoBlockDriver QCryptoBlockDriver;

struct QCryptoBlock {
    QCryptoBlockFormat format;

    const QCryptoBlockDriver *driver;
    void *opaque;

    QCryptoCipher *cipher;
    QCryptoIVGen *ivgen;
    QCryptoHashAlgorithm kdfhash;
    size_t niv;
    uint64_t payload_offset; /* In bytes */
};

struct QCryptoBlockDriver {
    int (*open)(QCryptoBlock *block,
                QCryptoBlockOpenOptions *options,
                const char *optprefix,
                QCryptoBlockReadFunc readfunc,
                void *opaque,
                unsigned int flags,
                Error **errp);

    int (*create)(QCryptoBlock *block,
                  QCryptoBlockCreateOptions *options,
                  const char *optprefix,
                  QCryptoBlockInitFunc initfunc,
                  QCryptoBlockWriteFunc writefunc,
                  void *opaque,
                  Error **errp);

    int (*get_info)(QCryptoBlock *block,
                    QCryptoBlockInfo *info,
                    Error **errp);

    void (*cleanup)(QCryptoBlock *block);

    int (*encrypt)(QCryptoBlock *block,
                   uint64_t startsector,
                   uint8_t *buf,
                   size_t len,
                   Error **errp);
    int (*decrypt)(QCryptoBlock *block,
                   uint64_t startsector,
                   uint8_t *buf,
                   size_t len,
                   Error **errp);

    bool (*has_format)(const uint8_t *buf,
                       size_t buflen);
};


int qcrypto_block_decrypt_helper(QCryptoCipher *cipher,
                                 size_t niv,
                                 QCryptoIVGen *ivgen,
                                 int sectorsize,
                                 uint64_t startsector,
                                 uint8_t *buf,
                                 size_t len,
                                 Error **errp);

int qcrypto_block_encrypt_helper(QCryptoCipher *cipher,
                                 size_t niv,
                                 QCryptoIVGen *ivgen,
                                 int sectorsize,
                                 uint64_t startsector,
                                 uint8_t *buf,
                                 size_t len,
                                 Error **errp);

#endif /* QCRYPTO_BLOCKPRIV_H */
