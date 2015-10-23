/*
 * QEMU block full disk encryption
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

#include "config-host.h"

#include "block/block_int.h"
#include "crypto/block.h"
#include "qapi/opts-visitor.h"
#include "qapi-visit.h"

#define FDE_OPT_LUKS_KEY_ID "keyid"
#define FDE_OPT_LUKS_CIPHER_ALG "cipher_alg"
#define FDE_OPT_LUKS_CIPHER_MODE "cipher_mode"
#define FDE_OPT_LUKS_IVGEN_ALG "ivgen_alg"
#define FDE_OPT_LUKS_IVGEN_HASH_ALG "ivgen_hash_alg"
#define FDE_OPT_LUKS_HASH_ALG "hash_alg"

typedef struct QBlockFDE QBlockFDE;

struct QBlockFDE {
    QCryptoBlock *block;
    CoMutex lock;
};


static int qblock_fde_probe_generic(QCryptoBlockFormat format,
                                    const uint8_t *buf,
                                    int buf_size,
                                    const char *filename)
{
    if (qcrypto_block_has_format(format,
                                 buf, buf_size)) {
        return 100;
    } else {
        return 0;
    }
}


static ssize_t qblock_fde_read_func(QCryptoBlock *block,
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


static ssize_t qblock_fde_write_func(QCryptoBlock *block,
                                     size_t offset,
                                     const uint8_t *buf,
                                     size_t buflen,
                                     Error **errp,
                                     void *opaque)
{
    BlockDriverState *bs = opaque;
    ssize_t ret;

    ret = bdrv_pwrite(bs, offset, buf, buflen);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not write encryption header");
        return ret;
    }
    return ret;
}


static QemuOptsList qblock_fde_runtime_opts_luks = {
    .name = "fde",
    .head = QTAILQ_HEAD_INITIALIZER(qblock_fde_runtime_opts_luks.head),
    .desc = {
        {
            .name = FDE_OPT_LUKS_KEY_ID,
            .type = QEMU_OPT_STRING,
            .help = "ID of the secret that provides the encryption key",
        },
        { /* end of list */ }
    },
};


static QemuOptsList qblock_fde_create_opts_luks = {
    .name = "fde",
    .head = QTAILQ_HEAD_INITIALIZER(qblock_fde_create_opts_luks.head),
    .desc = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Virtual disk size"
        },
        {
            .name = FDE_OPT_LUKS_KEY_ID,
            .type = QEMU_OPT_STRING,
            .help = "ID of the secret that provides the encryption key",
        },
        {
            .name = FDE_OPT_LUKS_CIPHER_ALG,
            .type = QEMU_OPT_STRING,
            .help = "Name of encryption cipher algorithm",
        },
        {
            .name = FDE_OPT_LUKS_CIPHER_MODE,
            .type = QEMU_OPT_STRING,
            .help = "Name of encryption cipher mode",
        },
        {
            .name = FDE_OPT_LUKS_IVGEN_ALG,
            .type = QEMU_OPT_STRING,
            .help = "Name of IV generator algorithm",
        },
        {
            .name = FDE_OPT_LUKS_IVGEN_HASH_ALG,
            .type = QEMU_OPT_STRING,
            .help = "Name of IV generator hash algorithm",
        },
        {
            .name = FDE_OPT_LUKS_HASH_ALG,
            .type = QEMU_OPT_STRING,
            .help = "Name of encryption hash algorithm",
        },
        { /* end of list */ }
    },
};


static QCryptoBlockOpenOptions *
qblock_fde_open_opts_init(QCryptoBlockFormat format,
                          QemuOpts *opts,
                          Error **errp)
{
    OptsVisitor *ov;
    QCryptoBlockOpenOptions *ret;
    Error *local_err = NULL;

    ret = g_new0(QCryptoBlockOpenOptions, 1);
    ret->format = format;

    ov = opts_visitor_new(opts);

    switch (format) {
    case Q_CRYPTO_BLOCK_FORMAT_LUKS:
        ret->u.luks = g_new0(QCryptoBlockOptionsLUKS, 1);
        visit_type_QCryptoBlockOptionsLUKS(opts_get_visitor(ov),
                                           &ret->u.luks, "luks", &local_err);
        break;

    default:
        error_setg(&local_err, "Unsupported block format %d", format);
        break;
    }

    if (local_err) {
        error_propagate(errp, local_err);
        opts_visitor_cleanup(ov);
        qapi_free_QCryptoBlockOpenOptions(ret);
        return NULL;
    }

    opts_visitor_cleanup(ov);
    return ret;
}


static QCryptoBlockCreateOptions *
qblock_fde_create_opts_init(QCryptoBlockFormat format,
                            QemuOpts *opts,
                            Error **errp)
{
    OptsVisitor *ov;
    QCryptoBlockCreateOptions *ret;
    Error *local_err = NULL;

    ret = g_new0(QCryptoBlockCreateOptions, 1);
    ret->format = format;

    ov = opts_visitor_new(opts);

    switch (format) {
    case Q_CRYPTO_BLOCK_FORMAT_LUKS:
        ret->u.luks = g_new0(QCryptoBlockCreateOptionsLUKS, 1);
        visit_type_QCryptoBlockCreateOptionsLUKS(
            opts_get_visitor(ov),
            &ret->u.luks, "luks", &local_err);
        break;

    default:
        error_setg(&local_err, "Unsupported block format %d", format);
        break;
    }

    if (local_err) {
        error_propagate(errp, local_err);
        opts_visitor_cleanup(ov);
        qapi_free_QCryptoBlockCreateOptions(ret);
        return NULL;
    }

    opts_visitor_cleanup(ov);
    return ret;
}


static int qblock_fde_open_generic(QCryptoBlockFormat format,
                                   QemuOptsList *opts_spec,
                                   BlockDriverState *bs,
                                   QDict *options,
                                   int flags,
                                   Error **errp)
{
    QBlockFDE *fde = bs->opaque;
    QemuOpts *opts = NULL;
    Error *local_err = NULL;
    int ret = -EINVAL;
    QCryptoBlockOpenOptions *open_opts = NULL;

    opts = qemu_opts_create(opts_spec, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto cleanup;
    }

    open_opts = qblock_fde_open_opts_init(format, opts, errp);
    if (!open_opts) {
        goto cleanup;
    }

    fde->block = qcrypto_block_open(open_opts,
                                    qblock_fde_read_func,
                                    bs,
                                    errp);

    if (!fde->block) {
        ret = -EIO;
        goto cleanup;
    }

    qemu_co_mutex_init(&fde->lock);

    ret = 0;
 cleanup:
    qapi_free_QCryptoBlockOpenOptions(open_opts);
    return ret;
}


static int qblock_fde_create_generic(QCryptoBlockFormat format,
                                     const char *filename,
                                     QemuOpts *opts,
                                     Error **errp)
{
    int ret = -EINVAL;
    QCryptoBlockCreateOptions *create_opts = NULL;
    BlockDriverState *bs = NULL;
    QCryptoBlock *fde = NULL;
    uint64_t size = 0;

    size = ROUND_UP(qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0),
                    BDRV_SECTOR_SIZE);

    create_opts = qblock_fde_create_opts_init(format, opts, errp);
    if (!create_opts) {
        return -1;
    }

    /* XXX Should we treat size as being total physical size
     * of the image (ie payload + encryption header), or just
     * the logical size of the image (ie payload). If the latter
     * then we need to extend 'size' to include the header
     * size */
    qemu_opt_set_number(opts, BLOCK_OPT_SIZE, size, &error_abort);
    ret = bdrv_create_file(filename, opts, errp);
    if (ret < 0) {
        goto cleanup;
    }

    ret = bdrv_open(&bs, filename, NULL, NULL, BDRV_O_RDWR | BDRV_O_PROTOCOL,
                    errp);
    if (ret < 0) {
        goto cleanup;
    }

    fde = qcrypto_block_create(create_opts,
                               qblock_fde_write_func,
                               bs,
                               errp);

    if (!fde) {
        ret = -EIO;
        goto cleanup;
    }

    ret = 0;
 cleanup:
    qcrypto_block_free(fde);
    bdrv_unref(bs);
    qapi_free_QCryptoBlockCreateOptions(create_opts);
    return ret;
}

static void qblock_fde_close(BlockDriverState *bs)
{
    QBlockFDE *fde = bs->opaque;
    qcrypto_block_free(fde->block);
}


#define QBLOCK_FDE_MAX_SECTORS 32

static coroutine_fn int
qblock_fde_co_readv(BlockDriverState *bs, int64_t sector_num,
                    int remaining_sectors, QEMUIOVector *qiov)
{
    QBlockFDE *fde = bs->opaque;
    int cur_nr_sectors; /* number of sectors in current iteration */
    uint64_t bytes_done = 0;
    uint8_t *cipher_data = NULL;
    QEMUIOVector hd_qiov;
    int ret = 0;
    size_t payload_offset = qcrypto_block_get_payload_offset(fde->block);

    qemu_iovec_init(&hd_qiov, qiov->niov);

    qemu_co_mutex_lock(&fde->lock);

    while (remaining_sectors) {
        cur_nr_sectors = remaining_sectors;

        if (cur_nr_sectors > QBLOCK_FDE_MAX_SECTORS) {
            cur_nr_sectors = QBLOCK_FDE_MAX_SECTORS;
        }
        cipher_data =
            qemu_try_blockalign(bs->file->bs, cur_nr_sectors * 512);

        qemu_iovec_reset(&hd_qiov);
        qemu_iovec_add(&hd_qiov, cipher_data, cur_nr_sectors * 512);

        qemu_co_mutex_unlock(&fde->lock);
        ret = bdrv_co_readv(bs->file->bs,
                            payload_offset + sector_num,
                            cur_nr_sectors, &hd_qiov);
        qemu_co_mutex_lock(&fde->lock);
        if (ret < 0) {
            goto cleanup;
        }

        if (qcrypto_block_decrypt(fde->block,
                                  sector_num,
                                  cipher_data, cur_nr_sectors * 512,
                                  NULL) < 0) {
            ret = -1;
            goto cleanup;
        }

        qemu_iovec_from_buf(qiov, bytes_done,
                            cipher_data, cur_nr_sectors * 512);

        remaining_sectors -= cur_nr_sectors;
        sector_num += cur_nr_sectors;
        bytes_done += cur_nr_sectors * 512;
    }

 cleanup:
    qemu_co_mutex_unlock(&fde->lock);

    qemu_iovec_destroy(&hd_qiov);
    qemu_vfree(cipher_data);

    return ret;
}


static coroutine_fn int
qblock_fde_co_writev(BlockDriverState *bs, int64_t sector_num,
                     int remaining_sectors, QEMUIOVector *qiov)
{
    QBlockFDE *fde = bs->opaque;
    int cur_nr_sectors; /* number of sectors in current iteration */
    uint64_t bytes_done = 0;
    uint8_t *cipher_data = NULL;
    QEMUIOVector hd_qiov;
    int ret = 0;
    size_t payload_offset = qcrypto_block_get_payload_offset(fde->block);

    qemu_iovec_init(&hd_qiov, qiov->niov);

    qemu_co_mutex_lock(&fde->lock);

    while (remaining_sectors) {
        cur_nr_sectors = remaining_sectors;

        if (cur_nr_sectors > QBLOCK_FDE_MAX_SECTORS) {
            cur_nr_sectors = QBLOCK_FDE_MAX_SECTORS;
        }
        cipher_data =
            qemu_try_blockalign(bs->file->bs, cur_nr_sectors * 512);

        qemu_iovec_to_buf(qiov, bytes_done,
                          cipher_data, cur_nr_sectors * 512);

        if (qcrypto_block_encrypt(fde->block,
                                  sector_num,
                                  cipher_data, cur_nr_sectors * 512,
                                  NULL) < 0) {
            ret = -1;
            goto cleanup;
        }

        qemu_iovec_reset(&hd_qiov);
        qemu_iovec_add(&hd_qiov, cipher_data, cur_nr_sectors * 512);

        qemu_co_mutex_unlock(&fde->lock);
        ret = bdrv_co_writev(bs->file->bs,
                             payload_offset + sector_num,
                             cur_nr_sectors, &hd_qiov);
        qemu_co_mutex_lock(&fde->lock);
        if (ret < 0) {
            goto cleanup;
        }

        remaining_sectors -= cur_nr_sectors;
        sector_num += cur_nr_sectors;
        bytes_done += cur_nr_sectors * 512;
    }

 cleanup:
    qemu_co_mutex_unlock(&fde->lock);

    qemu_iovec_destroy(&hd_qiov);
    qemu_vfree(cipher_data);

    return ret;
}


static int64_t qblock_fde_getlength(BlockDriverState *bs)
{
    QBlockFDE *fde = bs->opaque;
    int64_t len = bdrv_getlength(bs->file->bs);

    ssize_t offset = qcrypto_block_get_payload_offset(fde->block);

    len -= (offset * 512);

    return len;
}

#define QBLOCK_FDE_DRIVER(name, format)                                 \
    static int qblock_fde_probe_ ## name(const uint8_t *buf,            \
                                         int buf_size,                  \
                                         const char *filename) {        \
        return qblock_fde_probe_generic(format,                         \
                                        buf, buf_size, filename);       \
    }                                                                   \
                                                                        \
    static int qblock_fde_open_ ## name(BlockDriverState *bs,           \
                                        QDict *options,                 \
                                        int flags,                      \
                                        Error **errp)                   \
    {                                                                   \
        return qblock_fde_open_generic(format,                          \
                                       &qblock_fde_runtime_opts_ ## name, \
                                       bs, options, flags, errp);       \
    }                                                                   \
                                                                        \
    static int qblock_fde_create_ ## name(const char *filename,         \
                                          QemuOpts *opts,               \
                                          Error **errp)                 \
    {                                                                   \
        return qblock_fde_create_generic(format,                        \
                                         filename, opts, errp);         \
    }                                                                   \
                                                                        \
    BlockDriver bdrv_fde_ ## name = {                                   \
        .format_name        = #name,                                    \
        .instance_size      = sizeof(QBlockFDE),                        \
        .bdrv_probe         = qblock_fde_probe_ ## name,                \
        .bdrv_open          = qblock_fde_open_ ## name,                 \
        .bdrv_close         = qblock_fde_close,                         \
        .bdrv_create        = qblock_fde_create_ ## name,               \
        .create_opts        = &qblock_fde_create_opts_ ## name,         \
                                                                        \
        .bdrv_co_readv      = qblock_fde_co_readv,                      \
        .bdrv_co_writev     = qblock_fde_co_writev,                     \
        .bdrv_getlength     = qblock_fde_getlength,                     \
    }

QBLOCK_FDE_DRIVER(luks, Q_CRYPTO_BLOCK_FORMAT_LUKS);

static void qblock_fde_init(void)
{
    bdrv_register(&bdrv_fde_luks);
}

block_init(qblock_fde_init);
