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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "crypto/blockpriv.h"
#include "crypto/block-qcow.h"
#include "crypto/block-luks.h"

static const QCryptoBlockDriver *qcrypto_block_drivers[] = {
    [Q_CRYPTO_BLOCK_FORMAT_QCOW] = &qcrypto_block_driver_qcow,
    [Q_CRYPTO_BLOCK_FORMAT_LUKS] = &qcrypto_block_driver_luks,
};


bool qcrypto_block_has_format(QCryptoBlockFormat format,
                              const uint8_t *buf,
                              size_t len)
{
    const QCryptoBlockDriver *driver;

    if (format >= G_N_ELEMENTS(qcrypto_block_drivers) ||
        !qcrypto_block_drivers[format]) {
        return false;
    }

    driver = qcrypto_block_drivers[format];

    return driver->has_format(buf, len);
}


QCryptoBlock *qcrypto_block_open(QCryptoBlockOpenOptions *options,
                                 const char *optprefix,
                                 QCryptoBlockReadFunc readfunc,
                                 void *opaque,
                                 unsigned int flags,
                                 Error **errp)
{
    QCryptoBlock *block = g_new0(QCryptoBlock, 1);

    block->format = options->format;

    if (options->format >= G_N_ELEMENTS(qcrypto_block_drivers) ||
        !qcrypto_block_drivers[options->format]) {
        error_setg(errp, "Unsupported block driver %s",
                   QCryptoBlockFormat_lookup[options->format]);
        g_free(block);
        return NULL;
    }

    block->driver = qcrypto_block_drivers[options->format];

    if (block->driver->open(block, options, optprefix,
                            readfunc, opaque, flags, errp) < 0) {
        g_free(block);
        return NULL;
    }

    return block;
}


QCryptoBlock *qcrypto_block_create(QCryptoBlockCreateOptions *options,
                                   const char *optprefix,
                                   QCryptoBlockInitFunc initfunc,
                                   QCryptoBlockWriteFunc writefunc,
                                   void *opaque,
                                   Error **errp)
{
    QCryptoBlock *block = g_new0(QCryptoBlock, 1);

    block->format = options->format;

    if (options->format >= G_N_ELEMENTS(qcrypto_block_drivers) ||
        !qcrypto_block_drivers[options->format]) {
        error_setg(errp, "Unsupported block driver %s",
                   QCryptoBlockFormat_lookup[options->format]);
        g_free(block);
        return NULL;
    }

    block->driver = qcrypto_block_drivers[options->format];

    if (block->driver->create(block, options, optprefix, initfunc,
                              writefunc, opaque, errp) < 0) {
        g_free(block);
        return NULL;
    }

    return block;
}


QCryptoBlockInfo *qcrypto_block_get_info(QCryptoBlock *block,
                                         Error **errp)
{
    QCryptoBlockInfo *info = g_new0(QCryptoBlockInfo, 1);

    info->format = block->format;

    if (block->driver->get_info &&
        block->driver->get_info(block, info, errp) < 0) {
        g_free(info);
        return NULL;
    }

    return info;
}


int qcrypto_block_decrypt(QCryptoBlock *block,
                          uint64_t startsector,
                          uint8_t *buf,
                          size_t len,
                          Error **errp)
{
    return block->driver->decrypt(block, startsector, buf, len, errp);
}


int qcrypto_block_encrypt(QCryptoBlock *block,
                          uint64_t startsector,
                          uint8_t *buf,
                          size_t len,
                          Error **errp)
{
    return block->driver->encrypt(block, startsector, buf, len, errp);
}


QCryptoCipher *qcrypto_block_get_cipher(QCryptoBlock *block)
{
    return block->cipher;
}


QCryptoIVGen *qcrypto_block_get_ivgen(QCryptoBlock *block)
{
    return block->ivgen;
}


QCryptoHashAlgorithm qcrypto_block_get_kdf_hash(QCryptoBlock *block)
{
    return block->kdfhash;
}


uint64_t qcrypto_block_get_payload_offset(QCryptoBlock *block)
{
    return block->payload_offset;
}


void qcrypto_block_free(QCryptoBlock *block)
{
    if (!block) {
        return;
    }

    block->driver->cleanup(block);

    qcrypto_cipher_free(block->cipher);
    qcrypto_ivgen_free(block->ivgen);
    g_free(block);
}


int qcrypto_block_decrypt_helper(QCryptoCipher *cipher,
                                 size_t niv,
                                 QCryptoIVGen *ivgen,
                                 int sectorsize,
                                 uint64_t startsector,
                                 uint8_t *buf,
                                 size_t len,
                                 Error **errp)
{
    uint8_t *iv;
    int ret = -1;

    iv = niv ? g_new0(uint8_t, niv) : NULL;

    while (len > 0) {
        size_t nbytes;
        if (niv) {
            if (qcrypto_ivgen_calculate(ivgen,
                                        startsector,
                                        iv, niv,
                                        errp) < 0) {
                goto cleanup;
            }

            if (qcrypto_cipher_setiv(cipher,
                                     iv, niv,
                                     errp) < 0) {
                goto cleanup;
            }
        }

        nbytes = len > sectorsize ? sectorsize : len;
        if (qcrypto_cipher_decrypt(cipher, buf, buf,
                                   nbytes, errp) < 0) {
            goto cleanup;
        }

        startsector++;
        buf += nbytes;
        len -= nbytes;
    }

    ret = 0;
 cleanup:
    g_free(iv);
    return ret;
}


int qcrypto_block_encrypt_helper(QCryptoCipher *cipher,
                                 size_t niv,
                                 QCryptoIVGen *ivgen,
                                 int sectorsize,
                                 uint64_t startsector,
                                 uint8_t *buf,
                                 size_t len,
                                 Error **errp)
{
    uint8_t *iv;
    int ret = -1;

    iv = niv ? g_new0(uint8_t, niv) : NULL;

    while (len > 0) {
        size_t nbytes;
        if (niv) {
            if (qcrypto_ivgen_calculate(ivgen,
                                        startsector,
                                        iv, niv,
                                        errp) < 0) {
                goto cleanup;
            }

            if (qcrypto_cipher_setiv(cipher,
                                     iv, niv,
                                     errp) < 0) {
                goto cleanup;
            }
        }

        nbytes = len > sectorsize ? sectorsize : len;
        if (qcrypto_cipher_encrypt(cipher, buf, buf,
                                   nbytes, errp) < 0) {
            goto cleanup;
        }

        startsector++;
        buf += nbytes;
        len -= nbytes;
    }

    ret = 0;
 cleanup:
    g_free(iv);
    return ret;
}
