/*
 * QEMU Crypto block device encryption
 *
 * Copyright (c) 2015 Red Hat, Inc.
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

#ifndef QCRYPTO_BLOCK_PRIV_H__
#define QCRYPTO_BLOCK_PRIV_H__

#include "crypto/block.h"

typedef struct QCryptoBlockDriver QCryptoBlockDriver;

struct QCryptoBlock {
    QCryptoBlockFormat format;

    const QCryptoBlockDriver *driver;
    void *opaque;

    QCryptoCipher *cipher;
    QCryptoIVGen *ivgen;
    size_t niv;
    uint64_t payload_offset; /* In 512 byte sectors */
};

struct QCryptoBlockDriver {
    int (*open)(QCryptoBlock *block,
                QCryptoBlockOpenOptions *options,
                QCryptoBlockReadFunc readfunc,
                void *opaque,
                Error **errp);

    int (*create)(QCryptoBlock *block,
                  QCryptoBlockCreateOptions *options,
                  QCryptoBlockWriteFunc writefunc,
                  void *opaque,
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

    gboolean (*has_format)(const uint8_t *buf,
                           size_t buflen);
};


int qcrypto_block_decrypt_helper(QCryptoCipher *cipher,
                                 size_t niv,
                                 QCryptoIVGen *ivgen,
                                 uint64_t startsector,
                                 uint8_t *buf,
                                 size_t len,
                                 Error **errp);

int qcrypto_block_encrypt_helper(QCryptoCipher *cipher,
                                 size_t niv,
                                 QCryptoIVGen *ivgen,
                                 uint64_t startsector,
                                 uint8_t *buf,
                                 size_t len,
                                 Error **errp);

#endif /* QCRYPTO_BLOCK_PRIV_H__ */
