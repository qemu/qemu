/*
 * QEMU Crypto block device encryption
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "blockpriv.h"
#include "block-qcow.h"
#include "block-luks.h"

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
                                 size_t n_threads,
                                 Error **errp)
{
    QCryptoBlock *block = g_new0(QCryptoBlock, 1);

    block->format = options->format;

    if (options->format >= G_N_ELEMENTS(qcrypto_block_drivers) ||
        !qcrypto_block_drivers[options->format]) {
        error_setg(errp, "Unsupported block driver %s",
                   QCryptoBlockFormat_str(options->format));
        g_free(block);
        return NULL;
    }

    block->driver = qcrypto_block_drivers[options->format];

    if (block->driver->open(block, options, optprefix,
                            readfunc, opaque, flags, n_threads, errp) < 0)
    {
        g_free(block);
        return NULL;
    }

    qemu_mutex_init(&block->mutex);

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
                   QCryptoBlockFormat_str(options->format));
        g_free(block);
        return NULL;
    }

    block->driver = qcrypto_block_drivers[options->format];

    if (block->driver->create(block, options, optprefix, initfunc,
                              writefunc, opaque, errp) < 0) {
        g_free(block);
        return NULL;
    }

    qemu_mutex_init(&block->mutex);

    return block;
}


static ssize_t qcrypto_block_headerlen_hdr_init_func(QCryptoBlock *block,
        size_t headerlen, void *opaque, Error **errp)
{
    size_t *headerlenp = opaque;

    /* Stash away the payload size */
    *headerlenp = headerlen;
    return 0;
}


static ssize_t qcrypto_block_headerlen_hdr_write_func(QCryptoBlock *block,
        size_t offset, const uint8_t *buf, size_t buflen,
        void *opaque, Error **errp)
{
    /* Discard the bytes, we're not actually writing to an image */
    return buflen;
}


bool
qcrypto_block_calculate_payload_offset(QCryptoBlockCreateOptions *create_opts,
                                       const char *optprefix,
                                       size_t *len,
                                       Error **errp)
{
    /* Fake LUKS creation in order to determine the payload size */
    g_autoptr(QCryptoBlock) crypto =
        qcrypto_block_create(create_opts, optprefix,
                             qcrypto_block_headerlen_hdr_init_func,
                             qcrypto_block_headerlen_hdr_write_func,
                             len, errp);
    return crypto != NULL;
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
                          uint64_t offset,
                          uint8_t *buf,
                          size_t len,
                          Error **errp)
{
    return block->driver->decrypt(block, offset, buf, len, errp);
}


int qcrypto_block_encrypt(QCryptoBlock *block,
                          uint64_t offset,
                          uint8_t *buf,
                          size_t len,
                          Error **errp)
{
    return block->driver->encrypt(block, offset, buf, len, errp);
}


QCryptoCipher *qcrypto_block_get_cipher(QCryptoBlock *block)
{
    /* Ciphers should be accessed through pop/push method to be thread-safe.
     * Better, they should not be accessed externally at all (note, that
     * pop/push are static functions)
     * This function is used only in test with one thread (it's safe to skip
     * pop/push interface), so it's enough to assert it here:
     */
    assert(block->n_ciphers <= 1);
    return block->ciphers ? block->ciphers[0] : NULL;
}


static QCryptoCipher *qcrypto_block_pop_cipher(QCryptoBlock *block)
{
    QCryptoCipher *cipher;

    qemu_mutex_lock(&block->mutex);

    assert(block->n_free_ciphers > 0);
    block->n_free_ciphers--;
    cipher = block->ciphers[block->n_free_ciphers];

    qemu_mutex_unlock(&block->mutex);

    return cipher;
}


static void qcrypto_block_push_cipher(QCryptoBlock *block,
                                      QCryptoCipher *cipher)
{
    qemu_mutex_lock(&block->mutex);

    assert(block->n_free_ciphers < block->n_ciphers);
    block->ciphers[block->n_free_ciphers] = cipher;
    block->n_free_ciphers++;

    qemu_mutex_unlock(&block->mutex);
}


int qcrypto_block_init_cipher(QCryptoBlock *block,
                              QCryptoCipherAlgorithm alg,
                              QCryptoCipherMode mode,
                              const uint8_t *key, size_t nkey,
                              size_t n_threads, Error **errp)
{
    size_t i;

    assert(!block->ciphers && !block->n_ciphers && !block->n_free_ciphers);

    block->ciphers = g_new0(QCryptoCipher *, n_threads);

    for (i = 0; i < n_threads; i++) {
        block->ciphers[i] = qcrypto_cipher_new(alg, mode, key, nkey, errp);
        if (!block->ciphers[i]) {
            qcrypto_block_free_cipher(block);
            return -1;
        }
        block->n_ciphers++;
        block->n_free_ciphers++;
    }

    return 0;
}


void qcrypto_block_free_cipher(QCryptoBlock *block)
{
    size_t i;

    if (!block->ciphers) {
        return;
    }

    assert(block->n_ciphers == block->n_free_ciphers);

    for (i = 0; i < block->n_ciphers; i++) {
        qcrypto_cipher_free(block->ciphers[i]);
    }

    g_free(block->ciphers);
    block->ciphers = NULL;
    block->n_ciphers = block->n_free_ciphers = 0;
}

QCryptoIVGen *qcrypto_block_get_ivgen(QCryptoBlock *block)
{
    /* ivgen should be accessed under mutex. However, this function is used only
     * in test with one thread, so it's enough to assert it here:
     */
    assert(block->n_ciphers <= 1);
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


uint64_t qcrypto_block_get_sector_size(QCryptoBlock *block)
{
    return block->sector_size;
}


void qcrypto_block_free(QCryptoBlock *block)
{
    if (!block) {
        return;
    }

    block->driver->cleanup(block);

    qcrypto_block_free_cipher(block);
    qcrypto_ivgen_free(block->ivgen);
    qemu_mutex_destroy(&block->mutex);
    g_free(block);
}


typedef int (*QCryptoCipherEncDecFunc)(QCryptoCipher *cipher,
                                        const void *in,
                                        void *out,
                                        size_t len,
                                        Error **errp);

static int do_qcrypto_block_cipher_encdec(QCryptoCipher *cipher,
                                          size_t niv,
                                          QCryptoIVGen *ivgen,
                                          QemuMutex *ivgen_mutex,
                                          int sectorsize,
                                          uint64_t offset,
                                          uint8_t *buf,
                                          size_t len,
                                          QCryptoCipherEncDecFunc func,
                                          Error **errp)
{
    g_autofree uint8_t *iv = niv ? g_new0(uint8_t, niv) : NULL;
    int ret = -1;
    uint64_t startsector = offset / sectorsize;

    assert(QEMU_IS_ALIGNED(offset, sectorsize));
    assert(QEMU_IS_ALIGNED(len, sectorsize));

    while (len > 0) {
        size_t nbytes;
        if (niv) {
            if (ivgen_mutex) {
                qemu_mutex_lock(ivgen_mutex);
            }
            ret = qcrypto_ivgen_calculate(ivgen, startsector, iv, niv, errp);
            if (ivgen_mutex) {
                qemu_mutex_unlock(ivgen_mutex);
            }

            if (ret < 0) {
                return -1;
            }

            if (qcrypto_cipher_setiv(cipher,
                                     iv, niv,
                                     errp) < 0) {
                return -1;
            }
        }

        nbytes = len > sectorsize ? sectorsize : len;
        if (func(cipher, buf, buf, nbytes, errp) < 0) {
            return -1;
        }

        startsector++;
        buf += nbytes;
        len -= nbytes;
    }

    return 0;
}


int qcrypto_block_cipher_decrypt_helper(QCryptoCipher *cipher,
                                        size_t niv,
                                        QCryptoIVGen *ivgen,
                                        int sectorsize,
                                        uint64_t offset,
                                        uint8_t *buf,
                                        size_t len,
                                        Error **errp)
{
    return do_qcrypto_block_cipher_encdec(cipher, niv, ivgen, NULL, sectorsize,
                                          offset, buf, len,
                                          qcrypto_cipher_decrypt, errp);
}


int qcrypto_block_cipher_encrypt_helper(QCryptoCipher *cipher,
                                        size_t niv,
                                        QCryptoIVGen *ivgen,
                                        int sectorsize,
                                        uint64_t offset,
                                        uint8_t *buf,
                                        size_t len,
                                        Error **errp)
{
    return do_qcrypto_block_cipher_encdec(cipher, niv, ivgen, NULL, sectorsize,
                                          offset, buf, len,
                                          qcrypto_cipher_encrypt, errp);
}

int qcrypto_block_decrypt_helper(QCryptoBlock *block,
                                 int sectorsize,
                                 uint64_t offset,
                                 uint8_t *buf,
                                 size_t len,
                                 Error **errp)
{
    int ret;
    QCryptoCipher *cipher = qcrypto_block_pop_cipher(block);

    ret = do_qcrypto_block_cipher_encdec(cipher, block->niv, block->ivgen,
                                         &block->mutex, sectorsize, offset, buf,
                                         len, qcrypto_cipher_decrypt, errp);

    qcrypto_block_push_cipher(block, cipher);

    return ret;
}

int qcrypto_block_encrypt_helper(QCryptoBlock *block,
                                 int sectorsize,
                                 uint64_t offset,
                                 uint8_t *buf,
                                 size_t len,
                                 Error **errp)
{
    int ret;
    QCryptoCipher *cipher = qcrypto_block_pop_cipher(block);

    ret = do_qcrypto_block_cipher_encdec(cipher, block->niv, block->ivgen,
                                         &block->mutex, sectorsize, offset, buf,
                                         len, qcrypto_cipher_encrypt, errp);

    qcrypto_block_push_cipher(block, cipher);

    return ret;
}
