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
#include "qapi/qobject-input-visitor.h"
#include "qapi-visit.h"
#include "qapi/error.h"
#include "block/crypto.h"

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
                                      void *opaque,
                                      Error **errp)
{
    BlockDriverState *bs = opaque;
    ssize_t ret;

    ret = bdrv_pread(bs->file, offset, buf, buflen);
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
                                       void *opaque,
                                       Error **errp)
{
    struct BlockCryptoCreateData *data = opaque;
    ssize_t ret;

    ret = blk_pwrite(data->blk, offset, buf, buflen, 0);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not write encryption header");
        return ret;
    }
    return ret;
}


static ssize_t block_crypto_init_func(QCryptoBlock *block,
                                      size_t headerlen,
                                      void *opaque,
                                      Error **errp)
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
        BLOCK_CRYPTO_OPT_DEF_LUKS_KEY_SECRET(""),
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
        BLOCK_CRYPTO_OPT_DEF_LUKS_KEY_SECRET(""),
        BLOCK_CRYPTO_OPT_DEF_LUKS_CIPHER_ALG(""),
        BLOCK_CRYPTO_OPT_DEF_LUKS_CIPHER_MODE(""),
        BLOCK_CRYPTO_OPT_DEF_LUKS_IVGEN_ALG(""),
        BLOCK_CRYPTO_OPT_DEF_LUKS_IVGEN_HASH_ALG(""),
        BLOCK_CRYPTO_OPT_DEF_LUKS_HASH_ALG(""),
        BLOCK_CRYPTO_OPT_DEF_LUKS_ITER_TIME(""),
        { /* end of list */ }
    },
};


QCryptoBlockOpenOptions *
block_crypto_open_opts_init(QCryptoBlockFormat format,
                            QDict *opts,
                            Error **errp)
{
    Visitor *v;
    QCryptoBlockOpenOptions *ret = NULL;
    Error *local_err = NULL;

    ret = g_new0(QCryptoBlockOpenOptions, 1);
    ret->format = format;

    v = qobject_input_visitor_new_keyval(QOBJECT(opts));

    visit_start_struct(v, NULL, NULL, 0, &local_err);
    if (local_err) {
        goto out;
    }

    switch (format) {
    case Q_CRYPTO_BLOCK_FORMAT_LUKS:
        visit_type_QCryptoBlockOptionsLUKS_members(
            v, &ret->u.luks, &local_err);
        break;

    case Q_CRYPTO_BLOCK_FORMAT_QCOW:
        visit_type_QCryptoBlockOptionsQCow_members(
            v, &ret->u.qcow, &local_err);
        break;

    default:
        error_setg(&local_err, "Unsupported block format %d", format);
        break;
    }
    if (!local_err) {
        visit_check_struct(v, &local_err);
    }

    visit_end_struct(v, NULL);

 out:
    if (local_err) {
        error_propagate(errp, local_err);
        qapi_free_QCryptoBlockOpenOptions(ret);
        ret = NULL;
    }
    visit_free(v);
    return ret;
}


QCryptoBlockCreateOptions *
block_crypto_create_opts_init(QCryptoBlockFormat format,
                              QDict *opts,
                              Error **errp)
{
    Visitor *v;
    QCryptoBlockCreateOptions *ret = NULL;
    Error *local_err = NULL;

    ret = g_new0(QCryptoBlockCreateOptions, 1);
    ret->format = format;

    v = qobject_input_visitor_new_keyval(QOBJECT(opts));

    visit_start_struct(v, NULL, NULL, 0, &local_err);
    if (local_err) {
        goto out;
    }

    switch (format) {
    case Q_CRYPTO_BLOCK_FORMAT_LUKS:
        visit_type_QCryptoBlockCreateOptionsLUKS_members(
            v, &ret->u.luks, &local_err);
        break;

    case Q_CRYPTO_BLOCK_FORMAT_QCOW:
        visit_type_QCryptoBlockOptionsQCow_members(
            v, &ret->u.qcow, &local_err);
        break;

    default:
        error_setg(&local_err, "Unsupported block format %d", format);
        break;
    }
    if (!local_err) {
        visit_check_struct(v, &local_err);
    }

    visit_end_struct(v, NULL);

 out:
    if (local_err) {
        error_propagate(errp, local_err);
        qapi_free_QCryptoBlockCreateOptions(ret);
        ret = NULL;
    }
    visit_free(v);
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
    QDict *cryptoopts = NULL;

    bs->file = bdrv_open_child(NULL, options, "file", bs, &child_file,
                               false, errp);
    if (!bs->file) {
        return -EINVAL;
    }

    opts = qemu_opts_create(opts_spec, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto cleanup;
    }

    cryptoopts = qemu_opts_to_qdict(opts, NULL);

    open_opts = block_crypto_open_opts_init(format, cryptoopts, errp);
    if (!open_opts) {
        goto cleanup;
    }

    if (flags & BDRV_O_NO_IO) {
        cflags |= QCRYPTO_BLOCK_OPEN_NO_IO;
    }
    crypto->block = qcrypto_block_open(open_opts, NULL,
                                       block_crypto_read_func,
                                       bs,
                                       cflags,
                                       errp);

    if (!crypto->block) {
        ret = -EIO;
        goto cleanup;
    }

    bs->encrypted = true;

    ret = 0;
 cleanup:
    QDECREF(cryptoopts);
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
    QDict *cryptoopts;

    cryptoopts = qemu_opts_to_qdict(opts, NULL);

    create_opts = block_crypto_create_opts_init(format, cryptoopts, errp);
    if (!create_opts) {
        return -1;
    }

    crypto = qcrypto_block_create(create_opts, NULL,
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
    QDECREF(cryptoopts);
    qcrypto_block_free(crypto);
    blk_unref(data.blk);
    qapi_free_QCryptoBlockCreateOptions(create_opts);
    return ret;
}

static int block_crypto_truncate(BlockDriverState *bs, int64_t offset,
                                 PreallocMode prealloc, Error **errp)
{
    BlockCrypto *crypto = bs->opaque;
    size_t payload_offset =
        qcrypto_block_get_payload_offset(crypto->block);

    offset += payload_offset;

    return bdrv_truncate(bs->file, offset, prealloc, errp);
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

        ret = bdrv_co_readv(bs->file,
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

        ret = bdrv_co_writev(bs->file,
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

static int block_crypto_get_info_luks(BlockDriverState *bs,
                                      BlockDriverInfo *bdi)
{
    BlockDriverInfo subbdi;
    int ret;

    ret = bdrv_get_info(bs->file->bs, &subbdi);
    if (ret != 0) {
        return ret;
    }

    bdi->unallocated_blocks_are_zero = false;
    bdi->can_write_zeroes_with_unmap = false;
    bdi->cluster_size = subbdi.cluster_size;

    return 0;
}

static ImageInfoSpecific *
block_crypto_get_specific_info_luks(BlockDriverState *bs)
{
    BlockCrypto *crypto = bs->opaque;
    ImageInfoSpecific *spec_info;
    QCryptoBlockInfo *info;

    info = qcrypto_block_get_info(crypto->block, NULL);
    if (!info) {
        return NULL;
    }
    if (info->format != Q_CRYPTO_BLOCK_FORMAT_LUKS) {
        qapi_free_QCryptoBlockInfo(info);
        return NULL;
    }

    spec_info = g_new(ImageInfoSpecific, 1);
    spec_info->type = IMAGE_INFO_SPECIFIC_KIND_LUKS;
    spec_info->u.luks.data = g_new(QCryptoBlockInfoLUKS, 1);
    *spec_info->u.luks.data = info->u.luks;

    /* Blank out pointers we've just stolen to avoid double free */
    memset(&info->u.luks, 0, sizeof(info->u.luks));

    qapi_free_QCryptoBlockInfo(info);

    return spec_info;
}

BlockDriver bdrv_crypto_luks = {
    .format_name        = "luks",
    .instance_size      = sizeof(BlockCrypto),
    .bdrv_probe         = block_crypto_probe_luks,
    .bdrv_open          = block_crypto_open_luks,
    .bdrv_close         = block_crypto_close,
    .bdrv_child_perm    = bdrv_format_default_perms,
    .bdrv_create        = block_crypto_create_luks,
    .bdrv_truncate      = block_crypto_truncate,
    .create_opts        = &block_crypto_create_opts_luks,

    .bdrv_co_readv      = block_crypto_co_readv,
    .bdrv_co_writev     = block_crypto_co_writev,
    .bdrv_getlength     = block_crypto_getlength,
    .bdrv_get_info      = block_crypto_get_info_luks,
    .bdrv_get_specific_info = block_crypto_get_specific_info_luks,
};

static void block_crypto_init(void)
{
    bdrv_register(&bdrv_crypto_luks);
}

block_init(block_crypto_init);
