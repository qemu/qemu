/*
 * QEMU Crypto block device encryption QCow/QCow2 AES-CBC format
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

/*
 * Note that the block encryption implemented in this file is broken
 * by design. This exists only to allow data to be liberated from
 * existing qcow[2] images and should not be used in any new areas.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"

#include "block-qcow.h"
#include "crypto/secret.h"

#define QCRYPTO_BLOCK_QCOW_SECTOR_SIZE 512


static bool
qcrypto_block_qcow_has_format(const uint8_t *buf G_GNUC_UNUSED,
                              size_t buf_size G_GNUC_UNUSED)
{
    return false;
}


static int
qcrypto_block_qcow_init(QCryptoBlock *block,
                        const char *keysecret,
                        size_t n_threads,
                        Error **errp)
{
    char *password;
    int ret;
    uint8_t keybuf[16];
    int len;

    memset(keybuf, 0, 16);

    password = qcrypto_secret_lookup_as_utf8(keysecret, errp);
    if (!password) {
        return -1;
    }

    len = strlen(password);
    memcpy(keybuf, password, MIN(len, sizeof(keybuf)));
    g_free(password);

    block->niv = qcrypto_cipher_get_iv_len(QCRYPTO_CIPHER_ALG_AES_128,
                                           QCRYPTO_CIPHER_MODE_CBC);
    block->ivgen = qcrypto_ivgen_new(QCRYPTO_IVGEN_ALG_PLAIN64,
                                     0, 0, NULL, 0, errp);
    if (!block->ivgen) {
        ret = -ENOTSUP;
        goto fail;
    }

    ret = qcrypto_block_init_cipher(block, QCRYPTO_CIPHER_ALG_AES_128,
                                    QCRYPTO_CIPHER_MODE_CBC,
                                    keybuf, G_N_ELEMENTS(keybuf),
                                    n_threads, errp);
    if (ret < 0) {
        ret = -ENOTSUP;
        goto fail;
    }

    block->sector_size = QCRYPTO_BLOCK_QCOW_SECTOR_SIZE;
    block->payload_offset = 0;

    return 0;

 fail:
    qcrypto_block_free_cipher(block);
    qcrypto_ivgen_free(block->ivgen);
    return ret;
}


static int
qcrypto_block_qcow_open(QCryptoBlock *block,
                        QCryptoBlockOpenOptions *options,
                        const char *optprefix,
                        QCryptoBlockReadFunc readfunc G_GNUC_UNUSED,
                        void *opaque G_GNUC_UNUSED,
                        unsigned int flags,
                        size_t n_threads,
                        Error **errp)
{
    if (flags & QCRYPTO_BLOCK_OPEN_NO_IO) {
        block->sector_size = QCRYPTO_BLOCK_QCOW_SECTOR_SIZE;
        block->payload_offset = 0;
        return 0;
    } else {
        if (!options->u.qcow.key_secret) {
            error_setg(errp,
                       "Parameter '%skey-secret' is required for cipher",
                       optprefix ? optprefix : "");
            return -1;
        }
        return qcrypto_block_qcow_init(block, options->u.qcow.key_secret,
                                       n_threads, errp);
    }
}


static int
qcrypto_block_qcow_create(QCryptoBlock *block,
                          QCryptoBlockCreateOptions *options,
                          const char *optprefix,
                          QCryptoBlockInitFunc initfunc G_GNUC_UNUSED,
                          QCryptoBlockWriteFunc writefunc G_GNUC_UNUSED,
                          void *opaque G_GNUC_UNUSED,
                          Error **errp)
{
    if (!options->u.qcow.key_secret) {
        error_setg(errp, "Parameter '%skey-secret' is required for cipher",
                   optprefix ? optprefix : "");
        return -1;
    }
    /* QCow2 has no special header, since everything is hardwired */
    return qcrypto_block_qcow_init(block, options->u.qcow.key_secret, 1, errp);
}


static void
qcrypto_block_qcow_cleanup(QCryptoBlock *block)
{
}


static int
qcrypto_block_qcow_decrypt(QCryptoBlock *block,
                           uint64_t offset,
                           uint8_t *buf,
                           size_t len,
                           Error **errp)
{
    assert(QEMU_IS_ALIGNED(offset, QCRYPTO_BLOCK_QCOW_SECTOR_SIZE));
    assert(QEMU_IS_ALIGNED(len, QCRYPTO_BLOCK_QCOW_SECTOR_SIZE));
    return qcrypto_block_decrypt_helper(block,
                                        QCRYPTO_BLOCK_QCOW_SECTOR_SIZE,
                                        offset, buf, len, errp);
}


static int
qcrypto_block_qcow_encrypt(QCryptoBlock *block,
                           uint64_t offset,
                           uint8_t *buf,
                           size_t len,
                           Error **errp)
{
    assert(QEMU_IS_ALIGNED(offset, QCRYPTO_BLOCK_QCOW_SECTOR_SIZE));
    assert(QEMU_IS_ALIGNED(len, QCRYPTO_BLOCK_QCOW_SECTOR_SIZE));
    return qcrypto_block_encrypt_helper(block,
                                        QCRYPTO_BLOCK_QCOW_SECTOR_SIZE,
                                        offset, buf, len, errp);
}


const QCryptoBlockDriver qcrypto_block_driver_qcow = {
    .open = qcrypto_block_qcow_open,
    .create = qcrypto_block_qcow_create,
    .cleanup = qcrypto_block_qcow_cleanup,
    .decrypt = qcrypto_block_qcow_decrypt,
    .encrypt = qcrypto_block_qcow_encrypt,
    .has_format = qcrypto_block_qcow_has_format,
};
