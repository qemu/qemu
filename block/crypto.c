/*
 * QEMU block full disk encryption
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

#include "block/block_int.h"
#include "block/qdict.h"
#include "system/block-backend.h"
#include "crypto/block.h"
#include "qapi/opts-visitor.h"
#include "qapi/qapi-visit-crypto.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/cutils.h"
#include "qemu/memalign.h"
#include "crypto.h"

typedef struct BlockCrypto BlockCrypto;

struct BlockCrypto {
    QCryptoBlock *block;
    bool updating_keys;
    BdrvChild *header;  /* Reference to the detached LUKS header */
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


static int block_crypto_read_func(QCryptoBlock *block,
                                  size_t offset,
                                  uint8_t *buf,
                                  size_t buflen,
                                  void *opaque,
                                  Error **errp)
{
    BlockDriverState *bs = opaque;
    BlockCrypto *crypto = bs->opaque;
    ssize_t ret;

    GLOBAL_STATE_CODE();
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    ret = bdrv_pread(crypto->header ? crypto->header : bs->file,
                     offset, buflen, buf, 0);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not read encryption header");
        return ret;
    }
    return 0;
}

static int block_crypto_write_func(QCryptoBlock *block,
                                   size_t offset,
                                   const uint8_t *buf,
                                   size_t buflen,
                                   void *opaque,
                                   Error **errp)
{
    BlockDriverState *bs = opaque;
    BlockCrypto *crypto = bs->opaque;
    ssize_t ret;

    GLOBAL_STATE_CODE();
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    ret = bdrv_pwrite(crypto->header ? crypto->header : bs->file,
                      offset, buflen, buf, 0);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not write encryption header");
        return ret;
    }
    return 0;
}


struct BlockCryptoCreateData {
    BlockBackend *blk;
    uint64_t size;
    PreallocMode prealloc;
};


static int coroutine_fn GRAPH_UNLOCKED
block_crypto_create_write_func(QCryptoBlock *block, size_t offset,
                               const uint8_t *buf, size_t buflen, void *opaque,
                               Error **errp)
{
    struct BlockCryptoCreateData *data = opaque;
    ssize_t ret;

    ret = blk_pwrite(data->blk, offset, buflen, buf, 0);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not write encryption header");
        return ret;
    }
    return 0;
}

static int coroutine_fn GRAPH_UNLOCKED
block_crypto_create_init_func(QCryptoBlock *block, size_t headerlen,
                              void *opaque, Error **errp)
{
    struct BlockCryptoCreateData *data = opaque;
    Error *local_error = NULL;
    int ret;

    if (data->size > INT64_MAX || headerlen > INT64_MAX - data->size) {
        ret = -EFBIG;
        goto error;
    }

    /* User provided size should reflect amount of space made
     * available to the guest, so we must take account of that
     * which will be used by the crypto header
     */
    ret = blk_truncate(data->blk, data->size + headerlen, false,
                       data->prealloc, 0, &local_error);

    if (ret >= 0) {
        return 0;
    }

error:
    if (ret == -EFBIG) {
        /* Replace the error message with a better one */
        error_free(local_error);
        error_setg(errp, "The requested file size is too large");
    } else {
        error_propagate(errp, local_error);
    }

    return ret;
}

static int coroutine_fn GRAPH_UNLOCKED
block_crypto_co_format_luks_payload(BlockdevCreateOptionsLUKS *luks_opts,
                                    Error **errp)
{
    BlockDriverState *bs = NULL;
    BlockBackend *blk = NULL;
    Error *local_error = NULL;
    int ret;

    if (luks_opts->size > INT64_MAX) {
        return -EFBIG;
    }

    bs = bdrv_co_open_blockdev_ref(luks_opts->file, errp);
    if (bs == NULL) {
        return -EIO;
    }

    blk = blk_co_new_with_bs(bs, BLK_PERM_WRITE | BLK_PERM_RESIZE,
                             BLK_PERM_ALL, errp);
    if (!blk) {
        ret = -EPERM;
        goto fail;
    }

    ret = blk_truncate(blk, luks_opts->size, true,
                       luks_opts->preallocation, 0, &local_error);
    if (ret < 0) {
        if (ret == -EFBIG) {
            /* Replace the error message with a better one */
            error_free(local_error);
            error_setg(errp, "The requested file size is too large");
        }
        goto fail;
    }

    ret = 0;

fail:
    bdrv_co_unref(bs);
    return ret;
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
        BLOCK_CRYPTO_OPT_DEF_LUKS_DETACHED_HEADER(""),
        { /* end of list */ }
    },
};


static QemuOptsList block_crypto_amend_opts_luks = {
    .name = "crypto",
    .head = QTAILQ_HEAD_INITIALIZER(block_crypto_create_opts_luks.head),
    .desc = {
        BLOCK_CRYPTO_OPT_DEF_LUKS_STATE(""),
        BLOCK_CRYPTO_OPT_DEF_LUKS_KEYSLOT(""),
        BLOCK_CRYPTO_OPT_DEF_LUKS_OLD_SECRET(""),
        BLOCK_CRYPTO_OPT_DEF_LUKS_NEW_SECRET(""),
        BLOCK_CRYPTO_OPT_DEF_LUKS_ITER_TIME(""),
        { /* end of list */ }
    },
};

QCryptoBlockOpenOptions *
block_crypto_open_opts_init(QDict *opts, Error **errp)
{
    Visitor *v;
    QCryptoBlockOpenOptions *ret;

    v = qobject_input_visitor_new_flat_confused(opts, errp);
    if (!v) {
        return NULL;
    }

    visit_type_QCryptoBlockOpenOptions(v, NULL, &ret, errp);

    visit_free(v);
    return ret;
}


QCryptoBlockCreateOptions *
block_crypto_create_opts_init(QDict *opts, Error **errp)
{
    Visitor *v;
    QCryptoBlockCreateOptions *ret;

    v = qobject_input_visitor_new_flat_confused(opts, errp);
    if (!v) {
        return NULL;
    }

    visit_type_QCryptoBlockCreateOptions(v, NULL, &ret, errp);

    visit_free(v);
    return ret;
}

QCryptoBlockAmendOptions *
block_crypto_amend_opts_init(QDict *opts, Error **errp)
{
    Visitor *v;
    QCryptoBlockAmendOptions *ret;

    v = qobject_input_visitor_new_flat_confused(opts, errp);
    if (!v) {
        return NULL;
    }

    visit_type_QCryptoBlockAmendOptions(v, NULL, &ret, errp);

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
    ERRP_GUARD();

    BlockCrypto *crypto = bs->opaque;
    QemuOpts *opts = NULL;
    int ret;
    QCryptoBlockOpenOptions *open_opts = NULL;
    unsigned int cflags = 0;
    QDict *cryptoopts = NULL;

    GLOBAL_STATE_CODE();

    ret = bdrv_open_file_child(NULL, options, "file", bs, errp);
    if (ret < 0) {
        return ret;
    }

    crypto->header = bdrv_open_child(NULL, options, "header", bs,
                                     &child_of_bds, BDRV_CHILD_METADATA,
                                     true, errp);
    if (*errp != NULL) {
        return -EINVAL;
    }

    GRAPH_RDLOCK_GUARD_MAINLOOP();

    bs->supported_write_flags = BDRV_REQ_FUA &
        bs->file->bs->supported_write_flags;

    opts = qemu_opts_create(opts_spec, NULL, 0, &error_abort);
    if (!qemu_opts_absorb_qdict(opts, options, errp)) {
        ret = -EINVAL;
        goto cleanup;
    }

    cryptoopts = qemu_opts_to_qdict(opts, NULL);
    qdict_put_str(cryptoopts, "format", QCryptoBlockFormat_str(format));

    open_opts = block_crypto_open_opts_init(cryptoopts, errp);
    if (!open_opts) {
        ret = -EINVAL;
        goto cleanup;
    }

    if (flags & BDRV_O_NO_IO) {
        cflags |= QCRYPTO_BLOCK_OPEN_NO_IO;
    }
    if (crypto->header != NULL) {
        cflags |= QCRYPTO_BLOCK_OPEN_DETACHED;
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
    qobject_unref(cryptoopts);
    qapi_free_QCryptoBlockOpenOptions(open_opts);
    return ret;
}


static int coroutine_fn GRAPH_UNLOCKED
block_crypto_co_create_generic(BlockDriverState *bs, int64_t size,
                               QCryptoBlockCreateOptions *opts,
                               PreallocMode prealloc,
                               unsigned int flags,
                               Error **errp)
{
    int ret;
    BlockBackend *blk;
    QCryptoBlock *crypto = NULL;
    struct BlockCryptoCreateData data;

    blk = blk_co_new_with_bs(bs, BLK_PERM_WRITE | BLK_PERM_RESIZE, BLK_PERM_ALL,
                             errp);
    if (!blk) {
        ret = -EPERM;
        goto cleanup;
    }

    if (prealloc == PREALLOC_MODE_METADATA) {
        prealloc = PREALLOC_MODE_OFF;
    }

    data = (struct BlockCryptoCreateData) {
        .blk = blk,
        .size = flags & QCRYPTO_BLOCK_CREATE_DETACHED ? 0 : size,
        .prealloc = prealloc,
    };

    crypto = qcrypto_block_create(opts, NULL,
                                  block_crypto_create_init_func,
                                  block_crypto_create_write_func,
                                  &data,
                                  flags,
                                  errp);

    if (!crypto) {
        ret = -EIO;
        goto cleanup;
    }

    ret = 0;
 cleanup:
    qcrypto_block_free(crypto);
    blk_co_unref(blk);
    return ret;
}

static int coroutine_fn GRAPH_RDLOCK
block_crypto_co_truncate(BlockDriverState *bs, int64_t offset, bool exact,
                         PreallocMode prealloc, BdrvRequestFlags flags,
                         Error **errp)
{
    BlockCrypto *crypto = bs->opaque;
    uint64_t payload_offset =
        qcrypto_block_get_payload_offset(crypto->block);

    if (payload_offset > INT64_MAX - offset) {
        error_setg(errp, "The requested file size is too large");
        return -EFBIG;
    }

    offset += payload_offset;

    return bdrv_co_truncate(bs->file, offset, exact, prealloc, 0, errp);
}

static void block_crypto_close(BlockDriverState *bs)
{
    BlockCrypto *crypto = bs->opaque;
    qcrypto_block_free(crypto->block);
}

static int block_crypto_reopen_prepare(BDRVReopenState *state,
                                       BlockReopenQueue *queue, Error **errp)
{
    /* nothing needs checking */
    return 0;
}

/*
 * 1 MB bounce buffer gives good performance / memory tradeoff
 * when using cache=none|directsync.
 */
#define BLOCK_CRYPTO_MAX_IO_SIZE (1024 * 1024)

static int coroutine_fn GRAPH_RDLOCK
block_crypto_co_preadv(BlockDriverState *bs, int64_t offset, int64_t bytes,
                       QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    BlockCrypto *crypto = bs->opaque;
    uint64_t cur_bytes; /* number of bytes in current iteration */
    uint64_t bytes_done = 0;
    uint8_t *cipher_data = NULL;
    QEMUIOVector hd_qiov;
    int ret = 0;
    uint64_t sector_size = qcrypto_block_get_sector_size(crypto->block);
    uint64_t payload_offset = qcrypto_block_get_payload_offset(crypto->block);

    assert(payload_offset < INT64_MAX);
    assert(QEMU_IS_ALIGNED(offset, sector_size));
    assert(QEMU_IS_ALIGNED(bytes, sector_size));

    qemu_iovec_init(&hd_qiov, qiov->niov);

    /* Bounce buffer because we don't wish to expose cipher text
     * in qiov which points to guest memory.
     */
    cipher_data =
        qemu_try_blockalign(bs->file->bs, MIN(BLOCK_CRYPTO_MAX_IO_SIZE,
                                              qiov->size));
    if (cipher_data == NULL) {
        ret = -ENOMEM;
        goto cleanup;
    }

    while (bytes) {
        cur_bytes = MIN(bytes, BLOCK_CRYPTO_MAX_IO_SIZE);

        qemu_iovec_reset(&hd_qiov);
        qemu_iovec_add(&hd_qiov, cipher_data, cur_bytes);

        ret = bdrv_co_preadv(bs->file, payload_offset + offset + bytes_done,
                             cur_bytes, &hd_qiov, 0);
        if (ret < 0) {
            goto cleanup;
        }

        if (qcrypto_block_decrypt(crypto->block, offset + bytes_done,
                                  cipher_data, cur_bytes, NULL) < 0) {
            ret = -EIO;
            goto cleanup;
        }

        qemu_iovec_from_buf(qiov, bytes_done, cipher_data, cur_bytes);

        bytes -= cur_bytes;
        bytes_done += cur_bytes;
    }

 cleanup:
    qemu_iovec_destroy(&hd_qiov);
    qemu_vfree(cipher_data);

    return ret;
}


static int coroutine_fn GRAPH_RDLOCK
block_crypto_co_pwritev(BlockDriverState *bs, int64_t offset, int64_t bytes,
                        QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    BlockCrypto *crypto = bs->opaque;
    uint64_t cur_bytes; /* number of bytes in current iteration */
    uint64_t bytes_done = 0;
    uint8_t *cipher_data = NULL;
    QEMUIOVector hd_qiov;
    int ret = 0;
    uint64_t sector_size = qcrypto_block_get_sector_size(crypto->block);
    uint64_t payload_offset = qcrypto_block_get_payload_offset(crypto->block);

    flags &= ~BDRV_REQ_REGISTERED_BUF;

    assert(payload_offset < INT64_MAX);
    assert(QEMU_IS_ALIGNED(offset, sector_size));
    assert(QEMU_IS_ALIGNED(bytes, sector_size));

    qemu_iovec_init(&hd_qiov, qiov->niov);

    /* Bounce buffer because we're not permitted to touch
     * contents of qiov - it points to guest memory.
     */
    cipher_data =
        qemu_try_blockalign(bs->file->bs, MIN(BLOCK_CRYPTO_MAX_IO_SIZE,
                                              qiov->size));
    if (cipher_data == NULL) {
        ret = -ENOMEM;
        goto cleanup;
    }

    while (bytes) {
        cur_bytes = MIN(bytes, BLOCK_CRYPTO_MAX_IO_SIZE);

        qemu_iovec_to_buf(qiov, bytes_done, cipher_data, cur_bytes);

        if (qcrypto_block_encrypt(crypto->block, offset + bytes_done,
                                  cipher_data, cur_bytes, NULL) < 0) {
            ret = -EIO;
            goto cleanup;
        }

        qemu_iovec_reset(&hd_qiov);
        qemu_iovec_add(&hd_qiov, cipher_data, cur_bytes);

        ret = bdrv_co_pwritev(bs->file, payload_offset + offset + bytes_done,
                              cur_bytes, &hd_qiov, flags);
        if (ret < 0) {
            goto cleanup;
        }

        bytes -= cur_bytes;
        bytes_done += cur_bytes;
    }

 cleanup:
    qemu_iovec_destroy(&hd_qiov);
    qemu_vfree(cipher_data);

    return ret;
}

static void block_crypto_refresh_limits(BlockDriverState *bs, Error **errp)
{
    BlockCrypto *crypto = bs->opaque;
    uint64_t sector_size = qcrypto_block_get_sector_size(crypto->block);
    bs->bl.request_alignment = sector_size; /* No sub-sector I/O */
}


static int64_t coroutine_fn GRAPH_RDLOCK
block_crypto_co_getlength(BlockDriverState *bs)
{
    BlockCrypto *crypto = bs->opaque;
    int64_t len = bdrv_co_getlength(bs->file->bs);

    uint64_t offset = qcrypto_block_get_payload_offset(crypto->block);
    assert(offset < INT64_MAX);

    if (offset > len) {
        return -EIO;
    }

    len -= offset;

    return len;
}


static BlockMeasureInfo *block_crypto_measure(QemuOpts *opts,
                                              BlockDriverState *in_bs,
                                              Error **errp)
{
    g_autoptr(QCryptoBlockCreateOptions) create_opts = NULL;
    Error *local_err = NULL;
    BlockMeasureInfo *info;
    uint64_t size;
    size_t luks_payload_size;
    QDict *cryptoopts;

    /*
     * Preallocation mode doesn't affect size requirements but we must consume
     * the option.
     */
    g_free(qemu_opt_get_del(opts, BLOCK_OPT_PREALLOC));

    size = qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0);

    if (in_bs) {
        int64_t ssize = bdrv_getlength(in_bs);

        if (ssize < 0) {
            error_setg_errno(&local_err, -ssize,
                             "Unable to get image virtual_size");
            goto err;
        }

        size = ssize;
    }

    cryptoopts = qemu_opts_to_qdict_filtered(opts, NULL,
            &block_crypto_create_opts_luks, true);
    qdict_put_str(cryptoopts, "format", "luks");
    create_opts = block_crypto_create_opts_init(cryptoopts, &local_err);
    qobject_unref(cryptoopts);
    if (!create_opts) {
        goto err;
    }

    if (!qcrypto_block_calculate_payload_offset(create_opts, NULL,
                                                &luks_payload_size,
                                                &local_err)) {
        goto err;
    }

    /*
     * Unallocated blocks are still encrypted so allocation status makes no
     * difference to the file size.
     */
    info = g_new0(BlockMeasureInfo, 1);
    info->fully_allocated = luks_payload_size + size;
    info->required = luks_payload_size + size;
    return info;

err:
    error_propagate(errp, local_err);
    return NULL;
}


static int block_crypto_probe_luks(const uint8_t *buf,
                                   int buf_size,
                                   const char *filename) {
    return block_crypto_probe_generic(QCRYPTO_BLOCK_FORMAT_LUKS,
                                      buf, buf_size, filename);
}

static int block_crypto_open_luks(BlockDriverState *bs,
                                  QDict *options,
                                  int flags,
                                  Error **errp)
{
    return block_crypto_open_generic(QCRYPTO_BLOCK_FORMAT_LUKS,
                                     &block_crypto_runtime_opts_luks,
                                     bs, options, flags, errp);
}

static int coroutine_fn GRAPH_UNLOCKED
block_crypto_co_create_luks(BlockdevCreateOptions *create_options, Error **errp)
{
    BlockdevCreateOptionsLUKS *luks_opts;
    BlockDriverState *hdr_bs = NULL;
    BlockDriverState *bs = NULL;
    QCryptoBlockCreateOptions create_opts;
    PreallocMode preallocation = PREALLOC_MODE_OFF;
    unsigned int cflags = 0;
    int ret;

    assert(create_options->driver == BLOCKDEV_DRIVER_LUKS);
    luks_opts = &create_options->u.luks;

    if (luks_opts->header == NULL && luks_opts->file == NULL) {
        error_setg(errp, "Either the parameter 'header' or 'file' must "
                   "be specified");
        return -EINVAL;
    }

    if ((luks_opts->preallocation != PREALLOC_MODE_OFF) &&
        (luks_opts->file == NULL)) {
        error_setg(errp, "Parameter 'preallocation' requires 'file' to be "
                   "specified for formatting LUKS disk");
        return -EINVAL;
    }

    create_opts = (QCryptoBlockCreateOptions) {
        .format = QCRYPTO_BLOCK_FORMAT_LUKS,
        .u.luks = *qapi_BlockdevCreateOptionsLUKS_base(luks_opts),
    };

    if (luks_opts->has_preallocation) {
        preallocation = luks_opts->preallocation;
    }

    if (luks_opts->header) {
        /* LUKS volume with detached header */
        hdr_bs = bdrv_co_open_blockdev_ref(luks_opts->header, errp);
        if (hdr_bs == NULL) {
            return -EIO;
        }

        cflags |= QCRYPTO_BLOCK_CREATE_DETACHED;

        /* Format the LUKS header node */
        ret = block_crypto_co_create_generic(hdr_bs, 0, &create_opts,
                                             PREALLOC_MODE_OFF, cflags, errp);
        if (ret < 0) {
            goto fail;
        }

        /* Format the LUKS payload node */
        if (luks_opts->file) {
            ret = block_crypto_co_format_luks_payload(luks_opts, errp);
            if (ret < 0) {
                goto fail;
            }
        }
    } else if (luks_opts->file) {
        /* LUKS volume with none-detached header */
        bs = bdrv_co_open_blockdev_ref(luks_opts->file, errp);
        if (bs == NULL) {
            return -EIO;
        }

        ret = block_crypto_co_create_generic(bs, luks_opts->size, &create_opts,
                                             preallocation, cflags, errp);
        if (ret < 0) {
            goto fail;
        }
    }

    ret = 0;
fail:
    if (hdr_bs != NULL) {
        bdrv_co_unref(hdr_bs);
    }

    if (bs != NULL) {
        bdrv_co_unref(bs);
    }
    return ret;
}

static int coroutine_fn GRAPH_UNLOCKED
block_crypto_co_create_opts_luks(BlockDriver *drv, const char *filename,
                                 QemuOpts *opts, Error **errp)
{
    QCryptoBlockCreateOptions *create_opts = NULL;
    BlockDriverState *bs = NULL;
    QDict *cryptoopts;
    PreallocMode prealloc;
    char *buf = NULL;
    int64_t size;
    bool detached_hdr =
        qemu_opt_get_bool(opts, "detached-header", false);
    unsigned int cflags = 0;
    int ret;
    Error *local_err = NULL;

    /* Parse options */
    size = qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0);

    buf = qemu_opt_get_del(opts, BLOCK_OPT_PREALLOC);
    prealloc = qapi_enum_parse(&PreallocMode_lookup, buf,
                               PREALLOC_MODE_OFF, &local_err);
    g_free(buf);
    if (local_err) {
        error_propagate(errp, local_err);
        return -EINVAL;
    }

    cryptoopts = qemu_opts_to_qdict_filtered(opts, NULL,
                                             &block_crypto_create_opts_luks,
                                             true);

    qdict_put_str(cryptoopts, "format", "luks");
    create_opts = block_crypto_create_opts_init(cryptoopts, errp);
    if (!create_opts) {
        ret = -EINVAL;
        goto fail;
    }

    /* Create protocol layer */
    ret = bdrv_co_create_file(filename, opts, errp);
    if (ret < 0) {
        goto fail;
    }

    bs = bdrv_co_open(filename, NULL, NULL,
                      BDRV_O_RDWR | BDRV_O_RESIZE | BDRV_O_PROTOCOL, errp);
    if (!bs) {
        ret = -EINVAL;
        goto fail;
    }

    if (detached_hdr) {
        cflags |= QCRYPTO_BLOCK_CREATE_DETACHED;
    }

    /* Create format layer */
    ret = block_crypto_co_create_generic(bs, size, create_opts,
                                         prealloc, cflags, errp);
    if (ret < 0) {
        goto fail;
    }

    ret = 0;
fail:
    /*
     * If an error occurred, delete 'filename'. Even if the file existed
     * beforehand, it has been truncated and corrupted in the process.
     */
    if (ret) {
        bdrv_graph_co_rdlock();
        bdrv_co_delete_file_noerr(bs);
        bdrv_graph_co_rdunlock();
    }

    bdrv_co_unref(bs);
    qapi_free_QCryptoBlockCreateOptions(create_opts);
    qobject_unref(cryptoopts);
    return ret;
}

static int coroutine_fn GRAPH_RDLOCK
block_crypto_co_get_info_luks(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    BlockDriverInfo subbdi;
    int ret;

    ret = bdrv_co_get_info(bs->file->bs, &subbdi);
    if (ret != 0) {
        return ret;
    }

    bdi->cluster_size = subbdi.cluster_size;

    return 0;
}

static ImageInfoSpecific *
block_crypto_get_specific_info_luks(BlockDriverState *bs, Error **errp)
{
    BlockCrypto *crypto = bs->opaque;
    ImageInfoSpecific *spec_info;
    QCryptoBlockInfo *info;

    info = qcrypto_block_get_info(crypto->block, errp);
    if (!info) {
        return NULL;
    }
    assert(info->format == QCRYPTO_BLOCK_FORMAT_LUKS);

    spec_info = g_new(ImageInfoSpecific, 1);
    spec_info->type = IMAGE_INFO_SPECIFIC_KIND_LUKS;
    spec_info->u.luks.data = g_new(QCryptoBlockInfoLUKS, 1);
    *spec_info->u.luks.data = info->u.luks;

    /* Blank out pointers we've just stolen to avoid double free */
    memset(&info->u.luks, 0, sizeof(info->u.luks));

    qapi_free_QCryptoBlockInfo(info);

    return spec_info;
}

static int GRAPH_RDLOCK
block_crypto_amend_prepare(BlockDriverState *bs, Error **errp)
{
    BlockCrypto *crypto = bs->opaque;
    int ret;

    /* apply for exclusive read/write permissions to the underlying file */
    crypto->updating_keys = true;
    ret = bdrv_child_refresh_perms(bs, bs->file, errp);
    if (ret < 0) {
        /* Well, in this case we will not be updating any keys */
        crypto->updating_keys = false;
    }
    return ret;
}

static void GRAPH_RDLOCK
block_crypto_amend_cleanup(BlockDriverState *bs)
{
    BlockCrypto *crypto = bs->opaque;
    Error *errp = NULL;

    /* release exclusive read/write permissions to the underlying file */
    crypto->updating_keys = false;
    bdrv_child_refresh_perms(bs, bs->file, &errp);

    if (errp) {
        error_report_err(errp);
    }
}

static int
block_crypto_amend_options_generic_luks(BlockDriverState *bs,
                                        QCryptoBlockAmendOptions *amend_options,
                                        bool force,
                                        Error **errp)
{
    BlockCrypto *crypto = bs->opaque;

    assert(crypto);
    assert(crypto->block);

    return qcrypto_block_amend_options(crypto->block,
                                       block_crypto_read_func,
                                       block_crypto_write_func,
                                       bs,
                                       amend_options,
                                       force,
                                       errp);
}

static int GRAPH_RDLOCK
block_crypto_amend_options_luks(BlockDriverState *bs,
                                QemuOpts *opts,
                                BlockDriverAmendStatusCB *status_cb,
                                void *cb_opaque,
                                bool force,
                                Error **errp)
{
    BlockCrypto *crypto = bs->opaque;
    QDict *cryptoopts = NULL;
    QCryptoBlockAmendOptions *amend_options = NULL;
    int ret = -EINVAL;

    assert(crypto);
    assert(crypto->block);

    cryptoopts = qemu_opts_to_qdict(opts, NULL);
    qdict_put_str(cryptoopts, "format", "luks");
    amend_options = block_crypto_amend_opts_init(cryptoopts, errp);
    qobject_unref(cryptoopts);
    if (!amend_options) {
        goto cleanup;
    }

    ret = block_crypto_amend_prepare(bs, errp);
    if (ret) {
        goto perm_cleanup;
    }
    ret = block_crypto_amend_options_generic_luks(bs, amend_options,
                                                  force, errp);

perm_cleanup:
    block_crypto_amend_cleanup(bs);
cleanup:
    qapi_free_QCryptoBlockAmendOptions(amend_options);
    return ret;
}

static int
coroutine_fn block_crypto_co_amend_luks(BlockDriverState *bs,
                                        BlockdevAmendOptions *opts,
                                        bool force,
                                        Error **errp)
{
    QCryptoBlockAmendOptions amend_opts;

    amend_opts = (QCryptoBlockAmendOptions) {
        .format = QCRYPTO_BLOCK_FORMAT_LUKS,
        .u.luks = *qapi_BlockdevAmendOptionsLUKS_base(&opts->u.luks),
    };
    return block_crypto_amend_options_generic_luks(bs, &amend_opts,
                                                   force, errp);
}

static void
block_crypto_child_perms(BlockDriverState *bs, BdrvChild *c,
                         const BdrvChildRole role,
                         BlockReopenQueue *reopen_queue,
                         uint64_t perm, uint64_t shared,
                         uint64_t *nperm, uint64_t *nshared)
{

    BlockCrypto *crypto = bs->opaque;

    bdrv_default_perms(bs, c, role, reopen_queue, perm, shared, nperm, nshared);

    /*
     * For backward compatibility, manually share the write
     * and resize permission
     */
    *nshared |= shared & (BLK_PERM_WRITE | BLK_PERM_RESIZE);
    /*
     * Since we are not fully a format driver, don't always request
     * the read/resize permission but only when explicitly
     * requested
     */
    *nperm &= ~(BLK_PERM_WRITE | BLK_PERM_RESIZE);
    *nperm |= perm & (BLK_PERM_WRITE | BLK_PERM_RESIZE);

    /*
     * This driver doesn't modify LUKS metadata except
     * when updating the encryption slots.
     * Thus unlike a proper format driver we don't ask for
     * shared write/read permission. However we need it
     * when we are updating the keys, to ensure that only we
     * have access to the device.
     *
     * Encryption update will set the crypto->updating_keys
     * during that period and refresh permissions
     *
     */
    if (crypto->updating_keys) {
        /* need exclusive write access for header update */
        *nperm |= BLK_PERM_WRITE;
        /* unshare read and write permission */
        *nshared &= ~(BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE);
    }
}


static const char *const block_crypto_strong_runtime_opts[] = {
    BLOCK_CRYPTO_OPT_LUKS_KEY_SECRET,

    NULL
};

static BlockDriver bdrv_crypto_luks = {
    .format_name        = "luks",
    .instance_size      = sizeof(BlockCrypto),
    .bdrv_probe         = block_crypto_probe_luks,
    .bdrv_open          = block_crypto_open_luks,
    .bdrv_close         = block_crypto_close,
    .bdrv_child_perm    = block_crypto_child_perms,
    .bdrv_co_create     = block_crypto_co_create_luks,
    .bdrv_co_create_opts = block_crypto_co_create_opts_luks,
    .bdrv_co_truncate   = block_crypto_co_truncate,
    .create_opts        = &block_crypto_create_opts_luks,
    .amend_opts         = &block_crypto_amend_opts_luks,

    .bdrv_reopen_prepare = block_crypto_reopen_prepare,
    .bdrv_refresh_limits = block_crypto_refresh_limits,
    .bdrv_co_preadv     = block_crypto_co_preadv,
    .bdrv_co_pwritev    = block_crypto_co_pwritev,
    .bdrv_co_getlength  = block_crypto_co_getlength,
    .bdrv_measure       = block_crypto_measure,
    .bdrv_co_get_info   = block_crypto_co_get_info_luks,
    .bdrv_get_specific_info = block_crypto_get_specific_info_luks,
    .bdrv_amend_options = block_crypto_amend_options_luks,
    .bdrv_co_amend      = block_crypto_co_amend_luks,
    .bdrv_amend_pre_run = block_crypto_amend_prepare,
    .bdrv_amend_clean   = block_crypto_amend_cleanup,

    .is_format          = true,

    .strong_runtime_opts = block_crypto_strong_runtime_opts,
};

static void block_crypto_init(void)
{
    bdrv_register(&bdrv_crypto_luks);
}

block_init(block_crypto_init);
