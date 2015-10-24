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

#ifndef QCRYPTO_BLOCK_H__
#define QCRYPTO_BLOCK_H__

#include "crypto/cipher.h"
#include "crypto/ivgen.h"

typedef struct QCryptoBlock QCryptoBlock;

/* See also QCryptoBlockFormat, QCryptoBlockCreateOptions
 * and QCryptoBlockOpenOptions in qapi/crypto.json */

typedef ssize_t (*QCryptoBlockReadFunc)(QCryptoBlock *block,
                                        size_t offset,
                                        uint8_t *buf,
                                        size_t buflen,
                                        Error **errp,
                                        void *opaque);

typedef ssize_t (*QCryptoBlockWriteFunc)(QCryptoBlock *block,
                                         size_t offset,
                                         const uint8_t *buf,
                                         size_t buflen,
                                         Error **errp,
                                         void *opaque);

/**
 * qcrypto_block_has_format:
 * @format: the encryption format
 * @buf: the data from head of the volume
 * @len: the length of @buf in bytes
 *
 * Given @len bytes of data from the head of a storage volume
 * in @buf, probe to determine if the volume has the encryption
 * format specified in @format.
 *
 * Returns: true if the data in @buf matches @format
 */
gboolean qcrypto_block_has_format(QCryptoBlockFormat format,
                                  const uint8_t *buf,
                                  size_t buflen);

/**
 * qcrypto_block_open:
 * @options: the encryption options
 * @readfunc: callback for reading data from the volume
 * @opaque: data to pass to @readfunc
 * @errp: pointer to an uninitialized error object
 *
 * Create a new block encryption object for an existing
 * storage volume encrypted with format identified by
 * the parameters in @options.
 *
 * This will use @readfunc to initialize the encryption
 * context based on the volume header(s), extracting the
 * master key(s) as required.
 *
 * If any part of initializing the encryption context
 * fails an error will be returned. This could be due
 * to the volume being in the wrong format, an cipher
 * or IV generator algorithm that is not supoported,
 * or incorrect passphrases.
 *
 * Returns: a block encryption format, or NULL on error
 */
QCryptoBlock *qcrypto_block_open(QCryptoBlockOpenOptions *options,
                                 QCryptoBlockReadFunc readfunc,
                                 void *opaque,
                                 Error **errp);

/**
 * qcrypto_block_create:
 * @format: the encryption format
 * @keyid: ID of a QCryptoSecret with key for unlocking master key
 * @writefunc: callback for writing data to the volume header
 * @opaque: data to pass to @writefunc
 * @errp: pointer to an uninitialized error object
 *
 * Create a new block encryption object for initializing
 * a storage volume to be encrypted with format identified
 * by the parameters in @options.
 *
 * This method will write a new volume header using @writefunc,
 * generating new master keys, etc as required. Any existing
 * data present on the volume will be irrevokably destroyed.
 *
 * If any part of initializing the encryption context
 * fails an error will be returned. This could be due
 * to the volume being in the wrong format, an cipher
 * or IV generator algorithm that is not supoported,
 * or incorrect passphrases.
 *
 * Returns: a block encryption format, or NULL on error
 */
QCryptoBlock *qcrypto_block_create(QCryptoBlockCreateOptions *options,
                                   QCryptoBlockWriteFunc writefunc,
                                   void *opaque,
                                   Error **errp);

/**
 * @qcrypto_block_decrypt:
 * @block: the block encryption object
 * @startsector: the sector from which @buf was read
 * @buf: the buffer to decrypt
 * @len: the length of @buf in bytes
 * @errp: pointer to an uninitialized error object
 *
 * Decrypt @len bytes of cipher text in @buf, writing
 * plain text back into @buf
 *
 * Returns 0 on success, -1 on failure
 */
int qcrypto_block_decrypt(QCryptoBlock *block,
                          uint64_t startsector,
                          uint8_t *buf,
                          size_t len,
                          Error **errp);

/**
 * @qcrypto_block_encrypt:
 * @block: the block encryption object
 * @startsector: the sector to which @buf will be written
 * @buf: the buffer to decrypt
 * @len: the length of @buf in bytes
 * @errp: pointer to an uninitialized error object
 *
 * Encrypt @len bytes of plain text in @buf, writing
 * cipher text back into @buf
 *
 * Returns 0 on success, -1 on failure
 */
int qcrypto_block_encrypt(QCryptoBlock *block,
                          uint64_t startsector,
                          uint8_t *buf,
                          size_t len,
                          Error **errp);

/**
 * qcrypto_block_get_cipher:
 * @block: the block encryption object
 *
 * Get the cipher to use for payload encryption
 *
 * Returns: the cipher object
 */
QCryptoCipher *qcrypto_block_get_cipher(QCryptoBlock *block);

/**
 * qcrypto_block_get_ivgen:
 * @block: the block encryption object
 *
 * Get the initialization vector generator to use for
 * payload encryption
 *
 * Returns: the IV generator object
 */
QCryptoIVGen *qcrypto_block_get_ivgen(QCryptoBlock *block);

/**
 * qcrypto_block_get_payload_offset:
 * @block: the block encryption object
 *
 * Get the offset to the payload indicated by the
 * encryption header. The offset is measured in
 * 512 byte sectors
 *
 * Returns: the payload offset in sectors.
 */
uint64_t qcrypto_block_get_payload_offset(QCryptoBlock *block);

/**
 * qcrypto_block_free:
 * @block: the block encryption object
 *
 * Release all resources associated with the encryption
 * object
 */
void qcrypto_block_free(QCryptoBlock *block);

#endif /* QCRYPTO_BLOCK_H__ */
