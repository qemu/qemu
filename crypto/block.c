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
#include "qemu/lockable.h"
#include "blockpriv.h"
#include "block-qcow.h"
#include "block-luks.h"

static const QCryptoBlockDriver *qcrypto_block_drivers[] = {
    [QCRYPTO_BLOCK_FORMAT_QCOW] = &qcrypto_block_driver_qcow,
    [QCRYPTO_BLOCK_FORMAT_LUKS] = &qcrypto_block_driver_luks,
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

    qemu_mutex_init(&block->mutex);

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
                            readfunc, opaque, flags, errp) < 0)
    {
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
                                   unsigned int flags,
                                   Error **errp)
{
    QCryptoBlock *block = g_new0(QCryptoBlock, 1);

    qemu_mutex_init(&block->mutex);

    block->format = options->format;

    if (options->format >= G_N_ELEMENTS(qcrypto_block_drivers) ||
        !qcrypto_block_drivers[options->format]) {
        error_setg(errp, "Unsupported block driver %s",
                   QCryptoBlockFormat_str(options->format));
        g_free(block);
        return NULL;
    }

    block->driver = qcrypto_block_drivers[options->format];
    block->detached_header = flags & QCRYPTO_BLOCK_CREATE_DETACHED;

    if (block->driver->create(block, options, optprefix, initfunc,
                              writefunc, opaque, errp) < 0) {
        g_free(block);
        return NULL;
    }

    return block;
}


static int qcrypto_block_headerlen_hdr_init_func(QCryptoBlock *block,
        size_t headerlen, void *opaque, Error **errp)
{
    size_t *headerlenp = opaque;

    /* Stash away the payload size */
    *headerlenp = headerlen;
    return 0;
}


static int qcrypto_block_headerlen_hdr_write_func(QCryptoBlock *block,
        size_t offset, const uint8_t *buf, size_t buflen,
        void *opaque, Error **errp)
{
    /* Discard the bytes, we're not actually writing to an image */
    return 0;
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
                             len, 0, errp);
    return crypto != NULL;
}

int qcrypto_block_amend_options(QCryptoBlock *block,
                                QCryptoBlockReadFunc readfunc,
                                QCryptoBlockWriteFunc writefunc,
                                void *opaque,
                                QCryptoBlockAmendOptions *options,
                                bool force,
                                Error **errp)
{
    if (options->format != block->format) {
        error_setg(errp,
                   "Cannot amend encryption format");
        return -1;
    }

    if (!block->driver->amend) {
        error_setg(errp,
                   "Crypto format %s doesn't support format options amendment",
                   QCryptoBlockFormat_str(block->format));
        return -1;
    }

    return block->driver->amend(block,
                                readfunc,
                                writefunc,
                                opaque,
                                options,
                                force,
                                errp);
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
    assert(block->max_free_ciphers <= 1);
    return block->free_ciphers ? block->free_ciphers[0] : NULL;
}


static QCryptoCipher *qcrypto_block_pop_cipher(QCryptoBlock *block,
                                               Error **errp)
{
    /* Usually there is a free cipher available */
    WITH_QEMU_LOCK_GUARD(&block->mutex) {
        if (block->n_free_ciphers > 0) {
            block->n_free_ciphers--;
            return block->free_ciphers[block->n_free_ciphers];
        }
    }

    /* Otherwise allocate a new cipher */
    return qcrypto_cipher_new(block->alg, block->mode, block->key,
                              block->nkey, errp);
}


static void qcrypto_block_push_cipher(QCryptoBlock *block,
                                      QCryptoCipher *cipher)
{
    QEMU_LOCK_GUARD(&block->mutex);

    if (block->n_free_ciphers == block->max_free_ciphers) {
        block->max_free_ciphers++;
        block->free_ciphers = g_renew(QCryptoCipher *,
                                      block->free_ciphers,
                                      block->max_free_ciphers);
    }

    block->free_ciphers[block->n_free_ciphers] = cipher;
    block->n_free_ciphers++;
}


int qcrypto_block_init_cipher(QCryptoBlock *block,
                              QCryptoCipherAlgo alg,
                              QCryptoCipherMode mode,
                              const uint8_t *key, size_t nkey,
                              Error **errp)
{
    QCryptoCipher *cipher;

    assert(!block->free_ciphers && !block->max_free_ciphers &&
           !block->n_free_ciphers);

    /* Stash away cipher parameters for qcrypto_block_pop_cipher() */
    block->alg = alg;
    block->mode = mode;
    block->key = g_memdup2(key, nkey);
    block->nkey = nkey;

    /*
     * Create a new cipher to validate the parameters now. This reduces the
     * chance of cipher creation failing at I/O time.
     */
    cipher = qcrypto_block_pop_cipher(block, errp);
    if (!cipher) {
        g_free(block->key);
        block->key = NULL;
        return -1;
    }

    qcrypto_block_push_cipher(block, cipher);
    return 0;
}


void qcrypto_block_free_cipher(QCryptoBlock *block)
{
    size_t i;

    g_free(block->key);
    block->key = NULL;

    if (!block->free_ciphers) {
        return;
    }

    /* All popped ciphers were eventually pushed back */
    assert(block->n_free_ciphers == block->max_free_ciphers);

    for (i = 0; i < block->max_free_ciphers; i++) {
        qcrypto_cipher_free(block->free_ciphers[i]);
    }

    g_free(block->free_ciphers);
    block->free_ciphers = NULL;
    block->max_free_ciphers = block->n_free_ciphers = 0;
}

QCryptoIVGen *qcrypto_block_get_ivgen(QCryptoBlock *block)
{
    /* ivgen should be accessed under mutex. However, this function is used only
     * in test with one thread, so it's enough to assert it here:
     */
    assert(block->max_free_ciphers <= 1);
    return block->ivgen;
}


QCryptoHashAlgo qcrypto_block_get_kdf_hash(QCryptoBlock *block)
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
    QCryptoCipher *cipher = qcrypto_block_pop_cipher(block, errp);
    if (!cipher) {
        return -1;
    }

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
    QCryptoCipher *cipher = qcrypto_block_pop_cipher(block, errp);
    if (!cipher) {
        return -1;
    }

    ret = do_qcrypto_block_cipher_encdec(cipher, block->niv, block->ivgen,
                                         &block->mutex, sectorsize, offset, buf,
                                         len, qcrypto_cipher_encrypt, errp);

    qcrypto_block_push_cipher(block, cipher);

    return ret;
}
