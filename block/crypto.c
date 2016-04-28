/*
 * QEMU block full disk encryption
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

#include "block/block_int.h"
#include "sysemu/block-backend.h"
#include "crypto/block.h"
#include "qapi/opts-visitor.h"
#include "qapi-visit.h"
#include "qapi/error.h"

#define BLOCK_CRYPTO_OPT_LUKS_KEY_SECRET "key-secret"
#define BLOCK_CRYPTO_OPT_LUKS_CIPHER_ALG "cipher-alg"
#define BLOCK_CRYPTO_OPT_LUKS_CIPHER_MODE "cipher-mode"
#define BLOCK_CRYPTO_OPT_LUKS_IVGEN_ALG "ivgen-alg"
#define BLOCK_CRYPTO_OPT_LUKS_IVGEN_HASH_ALG "ivgen-hash-alg"
#define BLOCK_CRYPTO_OPT_LUKS_HASH_ALG "hash-alg"

typedef struct BlockCrypto BlockCrypto;

struct BlockCrypto {
    QCryptoBlock *block;
};


static int block_crypto_probe_generic(QCryptoBlockFormat format,
                                      const uint8_t *buf,
                                      int buf_size,
                                      const char *filename)
{
    if (qcrypto_block_has_format(format, buf, buf_size)) {
        return 100;
    } else {
        return 0;
    }
}


static ssize_t block_crypto_read_func(QCryptoBlock *block,
                                      size_t offset,
                                      uint8_t *buf,
                                      size_t buflen,
                                      Error **errp,
                                      void *opaque)
{
    BlockDriverState *bs = opaque;
    ssize_t ret;

    ret = bdrv_pread(bs->file->bs, offset, buf, buflen);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not read encryption header");
        return ret;
    }
    return ret;
}


struct BlockCryptoCreateData {
    const char *filename;
    QemuOpts *opts;
    BlockBackend *blk;
    uint64_t size;
};


static ssize_t block_crypto_write_func(QCryptoBlock *block,
                                       size_t offset,
                                       const uint8_t *buf,
                                       size_t buflen,
                                       Error **errp,
                                       void *opaque)
{
    struct BlockCryptoCreateData *data = opaque;
    ssize_t ret;

    ret = blk_pwrite(data->blk, offset, buf, buflen);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not write encryption header");
        return ret;
    }
    return ret;
}


static ssize_t block_crypto_init_func(QCryptoBlock *block,
                                      size_t headerlen,
                                      Error **errp,
                                      void *opaque)
{
    struct BlockCryptoCreateData *data = opaque;
    int ret;

    /* User provided size should reflect amount of space made
     * available to the guest, so we must take account of that
     * which will be used by the crypto header
     */
    data->size += headerlen;

    qemu_opt_set_number(data->opts, BLOCK_OPT_SIZE, data->size, &error_abort);
    ret = bdrv_create_file(data->filename, data->opts, errp);
    if (ret < 0) {
        return -1;
    }

    data->blk = blk_new_open(data->filename, NULL, NULL,
                             BDRV_O_RDWR | BDRV_O_PROTOCOL, errp);
    if (!data->blk) {
        return -1;
    }

    return 0;
}


static QemuOptsList block_crypto_runtime_opts_luks = {
    .name = "crypto",
    .head = QTAILQ_HEAD_INITIALIZER(block_crypto_runtime_opts_luks.head),
    .desc = {
        {
            .name = BLOCK_CRYPTO_OPT_LUKS_KEY_SECRET,
            .type = QEMU_OPT_STRING,
            .help = "ID of the secret that provides the encryption key",
        },
        { /* end of list */ }
    },
};


static QemuOptsList block_crypto_create_opts_luks = {
    .name = "crypto",
    .head = QTAILQ_HEAD_INITIALIZER(block_crypto_create_opts_luks.head),
    .desc = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Virtual disk size"
        },
        {
            .name = BLOCK_CRYPTO_OPT_LUKS_KEY_SECRET,
            .type = QEMU_OPT_STRING,
            .help = "ID of the secret that provides the encryption key",
        },
        {
            .name = BLOCK_CRYPTO_OPT_LUKS_CIPHER_ALG,
            .type = QEMU_OPT_STRING,
            .help = "Name of encryption cipher algorithm",
        },
        {
            .name = BLOCK_CRYPTO_OPT_LUKS_CIPHER_MODE,
            .type = QEMU_OPT_STRING,
            .help = "Name of encryption cipher mode",
        },
        {
            .name = BLOCK_CRYPTO_OPT_LUKS_IVGEN_ALG,
            .type = QEMU_OPT_STRING,
            .help = "Name of IV generator algorithm",
        },
        {
            .name = BLOCK_CRYPTO_OPT_LUKS_IVGEN_HASH_ALG,
            .type = QEMU_OPT_STRING,
            .help = "Name of IV generator hash algorithm",
        },
        {
            .name = BLOCK_CRYPTO_OPT_LUKS_HASH_ALG,
            .type = QEMU_OPT_STRING,
            .help = "Name of encryption hash algorithm",
        },
        { /* end of list */ }
    },
};


static QCryptoBlockOpenOptions *
block_crypto_open_opts_init(QCryptoBlockFormat format,
                            QemuOpts *opts,
                            Error **errp)
{
    OptsVisitor *ov;
    QCryptoBlockOpenOptions *ret = NULL;
    Error *local_err = NULL;

    ret = g_new0(QCryptoBlockOpenOptions, 1);
    ret->format = format;

    ov = opts_visitor_new(opts);

    visit_start_struct(opts_get_visitor(ov),
                       NULL, NULL, 0, &local_err);
    if (local_err) {
        goto out;
    }

    switch (format) {
    case Q_CRYPTO_BLOCK_FORMAT_LUKS:
        visit_type_QCryptoBlockOptionsLUKS_members(
            opts_get_visitor(ov), &ret->u.luks, &local_err);
        break;

    default:
        error_setg(&local_err, "Unsupported block format %d", format);
        break;
    }
    if (!local_err) {
        visit_check_struct(opts_get_visitor(ov), &local_err);
    }

    visit_end_struct(opts_get_visitor(ov));

 out:
    if (local_err) {
        error_propagate(errp, local_err);
        qapi_free_QCryptoBlockOpenOptions(ret);
        ret = NULL;
    }
    opts_visitor_cleanup(ov);
    return ret;
}


static QCryptoBlockCreateOptions *
block_crypto_create_opts_init(QCryptoBlockFormat format,
                              QemuOpts *opts,
                              Error **errp)
{
    OptsVisitor *ov;
    QCryptoBlockCreateOptions *ret = NULL;
    Error *local_err = NULL;

    ret = g_new0(QCryptoBlockCreateOptions, 1);
    ret->format = format;

    ov = opts_visitor_new(opts);

    visit_start_struct(opts_get_visitor(ov),
                       NULL, NULL, 0, &local_err);
    if (local_err) {
        goto out;
    }

    switch (format) {
    case Q_CRYPTO_BLOCK_FORMAT_LUKS:
        visit_type_QCryptoBlockCreateOptionsLUKS_members(
            opts_get_visitor(ov), &ret->u.luks, &local_err);
        break;

    default:
        error_setg(&local_err, "Unsupported block format %d", format);
        break;
    }
    if (!local_err) {
        visit_check_struct(opts_get_visitor(ov), &local_err);
    }

    visit_end_struct(opts_get_visitor(ov));

 out:
    if (local_err) {
        error_propagate(errp, local_err);
        qapi_free_QCryptoBlockCreateOptions(ret);
        ret = NULL;
    }
    opts_visitor_cleanup(ov);
    return ret;
}


static int block_crypto_open_generic(QCryptoBlockFormat format,
                                     QemuOptsList *opts_spec,
                                     BlockDriverState *bs,
                                     QDict *options,
                                     int flags,
                                     Error **errp)
{
    BlockCrypto *crypto = bs->opaque;
    QemuOpts *opts = NULL;
    Error *local_err = NULL;
    int ret = -EINVAL;
    QCryptoBlockOpenOptions *open_opts = NULL;
    unsigned int cflags = 0;

    opts = qemu_opts_create(opts_spec, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto cleanup;
    }

    open_opts = block_crypto_open_opts_init(format, opts, errp);
    if (!open_opts) {
        goto cleanup;
    }

    if (flags & BDRV_O_NO_IO) {
        cflags |= QCRYPTO_BLOCK_OPEN_NO_IO;
    }
    crypto->block = qcrypto_block_open(open_opts,
                                       block_crypto_read_func,
                                       bs,
                                       cflags,
                                       errp);

    if (!crypto->block) {
        ret = -EIO;
        goto cleanup;
    }

    bs->encrypted = 1;
    bs->valid_key = 1;

    ret = 0;
 cleanup:
    qapi_free_QCryptoBlockOpenOptions(open_opts);
    return ret;
}


static int block_crypto_create_generic(QCryptoBlockFormat format,
                                       const char *filename,
                                       QemuOpts *opts,
                                       Error **errp)
{
    int ret = -EINVAL;
    QCryptoBlockCreateOptions *create_opts = NULL;
    QCryptoBlock *crypto = NULL;
    struct BlockCryptoCreateData data = {
        .size = ROUND_UP(qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0),
                         BDRV_SECTOR_SIZE),
        .opts = opts,
        .filename = filename,
    };

    create_opts = block_crypto_create_opts_init(format, opts, errp);
    if (!create_opts) {
        return -1;
    }

    crypto = qcrypto_block_create(create_opts,
                                  block_crypto_init_func,
                                  block_crypto_write_func,
                                  &data,
                                  errp);

    if (!crypto) {
        ret = -EIO;
        goto cleanup;
    }

    ret = 0;
 cleanup:
    qcrypto_block_free(crypto);
    blk_unref(data.blk);
    qapi_free_QCryptoBlockCreateOptions(create_opts);
    return ret;
}

static int block_crypto_truncate(BlockDriverState *bs, int64_t offset)
{
    BlockCrypto *crypto = bs->opaque;
    size_t payload_offset =
        qcrypto_block_get_payload_offset(crypto->block);

    offset += payload_offset;

    return bdrv_truncate(bs->file->bs, offset);
}

static void block_crypto_close(BlockDriverState *bs)
{
    BlockCrypto *crypto = bs->opaque;
    qcrypto_block_free(crypto->block);
}


#define BLOCK_CRYPTO_MAX_SECTORS 32

static coroutine_fn int
block_crypto_co_readv(BlockDriverState *bs, int64_t sector_num,
                      int remaining_sectors, QEMUIOVector *qiov)
{
    BlockCrypto *crypto = bs->opaque;
    int cur_nr_sectors; /* number of sectors in current iteration */
    uint64_t bytes_done = 0;
    uint8_t *cipher_data = NULL;
    QEMUIOVector hd_qiov;
    int ret = 0;
    size_t payload_offset =
        qcrypto_block_get_payload_offset(crypto->block) / 512;

    qemu_iovec_init(&hd_qiov, qiov->niov);

    /* Bounce buffer so we have a linear mem region for
     * entire sector. XXX optimize so we avoid bounce
     * buffer in case that qiov->niov == 1
     */
    cipher_data =
        qemu_try_blockalign(bs->file->bs, MIN(BLOCK_CRYPTO_MAX_SECTORS * 512,
                                              qiov->size));
    if (cipher_data == NULL) {
        ret = -ENOMEM;
        goto cleanup;
    }

    while (remaining_sectors) {
        cur_nr_sectors = remaining_sectors;

        if (cur_nr_sectors > BLOCK_CRYPTO_MAX_SECTORS) {
            cur_nr_sectors = BLOCK_CRYPTO_MAX_SECTORS;
        }

        qemu_iovec_reset(&hd_qiov);
        qemu_iovec_add(&hd_qiov, cipher_data, cur_nr_sectors * 512);

        ret = bdrv_co_readv(bs->file->bs,
                            payload_offset + sector_num,
                            cur_nr_sectors, &hd_qiov);
        if (ret < 0) {
            goto cleanup;
        }

        if (qcrypto_block_decrypt(crypto->block,
                                  sector_num,
                                  cipher_data, cur_nr_sectors * 512,
                                  NULL) < 0) {
            ret = -EIO;
            goto cleanup;
        }

        qemu_iovec_from_buf(qiov, bytes_done,
                            cipher_data, cur_nr_sectors * 512);

        remaining_sectors -= cur_nr_sectors;
        sector_num += cur_nr_sectors;
        bytes_done += cur_nr_sectors * 512;
    }

 cleanup:
    qemu_iovec_destroy(&hd_qiov);
    qemu_vfree(cipher_data);

    return ret;
}


static coroutine_fn int
block_crypto_co_writev(BlockDriverState *bs, int64_t sector_num,
                       int remaining_sectors, QEMUIOVector *qiov)
{
    BlockCrypto *crypto = bs->opaque;
    int cur_nr_sectors; /* number of sectors in current iteration */
    uint64_t bytes_done = 0;
    uint8_t *cipher_data = NULL;
    QEMUIOVector hd_qiov;
    int ret = 0;
    size_t payload_offset =
        qcrypto_block_get_payload_offset(crypto->block) / 512;

    qemu_iovec_init(&hd_qiov, qiov->niov);

    /* Bounce buffer so we have a linear mem region for
     * entire sector. XXX optimize so we avoid bounce
     * buffer in case that qiov->niov == 1
     */
    cipher_data =
        qemu_try_blockalign(bs->file->bs, MIN(BLOCK_CRYPTO_MAX_SECTORS * 512,
                                              qiov->size));
    if (cipher_data == NULL) {
        ret = -ENOMEM;
        goto cleanup;
    }

    while (remaining_sectors) {
        cur_nr_sectors = remaining_sectors;

        if (cur_nr_sectors > BLOCK_CRYPTO_MAX_SECTORS) {
            cur_nr_sectors = BLOCK_CRYPTO_MAX_SECTORS;
        }

        qemu_iovec_to_buf(qiov, bytes_done,
                          cipher_data, cur_nr_sectors * 512);

        if (qcrypto_block_encrypt(crypto->block,
                                  sector_num,
                                  cipher_data, cur_nr_sectors * 512,
                                  NULL) < 0) {
            ret = -EIO;
            goto cleanup;
        }

        qemu_iovec_reset(&hd_qiov);
        qemu_iovec_add(&hd_qiov, cipher_data, cur_nr_sectors * 512);

        ret = bdrv_co_writev(bs->file->bs,
                             payload_offset + sector_num,
                             cur_nr_sectors, &hd_qiov);
        if (ret < 0) {
            goto cleanup;
        }

        remaining_sectors -= cur_nr_sectors;
        sector_num += cur_nr_sectors;
        bytes_done += cur_nr_sectors * 512;
    }

 cleanup:
    qemu_iovec_destroy(&hd_qiov);
    qemu_vfree(cipher_data);

    return ret;
}


static int64_t block_crypto_getlength(BlockDriverState *bs)
{
    BlockCrypto *crypto = bs->opaque;
    int64_t len = bdrv_getlength(bs->file->bs);

    ssize_t offset = qcrypto_block_get_payload_offset(crypto->block);

    len -= offset;

    return len;
}


static int block_crypto_probe_luks(const uint8_t *buf,
                                   int buf_size,
                                   const char *filename) {
    return block_crypto_probe_generic(Q_CRYPTO_BLOCK_FORMAT_LUKS,
                                      buf, buf_size, filename);
}

static int block_crypto_open_luks(BlockDriverState *bs,
                                  QDict *options,
                                  int flags,
                                  Error **errp)
{
    return block_crypto_open_generic(Q_CRYPTO_BLOCK_FORMAT_LUKS,
                                     &block_crypto_runtime_opts_luks,
                                     bs, options, flags, errp);
}

static int block_crypto_create_luks(const char *filename,
                                    QemuOpts *opts,
                                    Error **errp)
{
    return block_crypto_create_generic(Q_CRYPTO_BLOCK_FORMAT_LUKS,
                                       filename, opts, errp);
}

BlockDriver bdrv_crypto_luks = {
    .format_name        = "luks",
    .instance_size      = sizeof(BlockCrypto),
    .bdrv_probe         = block_crypto_probe_luks,
    .bdrv_open          = block_crypto_open_luks,
    .bdrv_close         = block_crypto_close,
    .bdrv_create        = block_crypto_create_luks,
    .bdrv_truncate      = block_crypto_truncate,
    .create_opts        = &block_crypto_create_opts_luks,

    .bdrv_co_readv      = block_crypto_co_readv,
    .bdrv_co_writev     = block_crypto_co_writev,
    .bdrv_getlength     = block_crypto_getlength,
};

static void block_crypto_init(void)
{
    bdrv_register(&bdrv_crypto_luks);
}

block_init(block_crypto_init);
