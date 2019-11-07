/* BlockDriver implementation for "raw" format driver
 *
 * Copyright (C) 2010-2016 Red Hat, Inc.
 * Copyright (C) 2010, Blue Swirl <blauwirbel@gmail.com>
 * Copyright (C) 2009, Anthony Liguori <aliguori@us.ibm.com>
 *
 * Author:
 *   Laszlo Ersek <lersek@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "block/block_int.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/option.h"

typedef struct BDRVRawState {
    uint64_t offset;
    uint64_t size;
    bool has_size;
} BDRVRawState;

static const char *const mutable_opts[] = { "offset", "size", NULL };

static QemuOptsList raw_runtime_opts = {
    .name = "raw",
    .head = QTAILQ_HEAD_INITIALIZER(raw_runtime_opts.head),
    .desc = {
        {
            .name = "offset",
            .type = QEMU_OPT_SIZE,
            .help = "offset in the disk where the image starts",
        },
        {
            .name = "size",
            .type = QEMU_OPT_SIZE,
            .help = "virtual disk size",
        },
        { /* end of list */ }
    },
};

static QemuOptsList raw_create_opts = {
    .name = "raw-create-opts",
    .head = QTAILQ_HEAD_INITIALIZER(raw_create_opts.head),
    .desc = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Virtual disk size"
        },
        { /* end of list */ }
    }
};

static int raw_read_options(QDict *options, BlockDriverState *bs,
    BDRVRawState *s, Error **errp)
{
    Error *local_err = NULL;
    QemuOpts *opts = NULL;
    int64_t real_size = 0;
    int ret;

    real_size = bdrv_getlength(bs->file->bs);
    if (real_size < 0) {
        error_setg_errno(errp, -real_size, "Could not get image size");
        return real_size;
    }

    opts = qemu_opts_create(&raw_runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto end;
    }

    s->offset = qemu_opt_get_size(opts, "offset", 0);
    if (s->offset > real_size) {
        error_setg(errp, "Offset (%" PRIu64 ") cannot be greater than "
            "size of the containing file (%" PRId64 ")",
            s->offset, real_size);
        ret = -EINVAL;
        goto end;
    }

    if (qemu_opt_find(opts, "size") != NULL) {
        s->size = qemu_opt_get_size(opts, "size", 0);
        s->has_size = true;
    } else {
        s->has_size = false;
        s->size = real_size - s->offset;
    }

    /* Check size and offset */
    if ((real_size - s->offset) < s->size) {
        error_setg(errp, "The sum of offset (%" PRIu64 ") and size "
            "(%" PRIu64 ") has to be smaller or equal to the "
            " actual size of the containing file (%" PRId64 ")",
            s->offset, s->size, real_size);
        ret = -EINVAL;
        goto end;
    }

    /* Make sure size is multiple of BDRV_SECTOR_SIZE to prevent rounding
     * up and leaking out of the specified area. */
    if (s->has_size && !QEMU_IS_ALIGNED(s->size, BDRV_SECTOR_SIZE)) {
        error_setg(errp, "Specified size is not multiple of %llu",
            BDRV_SECTOR_SIZE);
        ret = -EINVAL;
        goto end;
    }

    ret = 0;

end:

    qemu_opts_del(opts);

    return ret;
}

static int raw_reopen_prepare(BDRVReopenState *reopen_state,
                              BlockReopenQueue *queue, Error **errp)
{
    assert(reopen_state != NULL);
    assert(reopen_state->bs != NULL);

    reopen_state->opaque = g_new0(BDRVRawState, 1);

    return raw_read_options(
        reopen_state->options,
        reopen_state->bs,
        reopen_state->opaque,
        errp);
}

static void raw_reopen_commit(BDRVReopenState *state)
{
    BDRVRawState *new_s = state->opaque;
    BDRVRawState *s = state->bs->opaque;

    memcpy(s, new_s, sizeof(BDRVRawState));

    g_free(state->opaque);
    state->opaque = NULL;
}

static void raw_reopen_abort(BDRVReopenState *state)
{
    g_free(state->opaque);
    state->opaque = NULL;
}

/* Check and adjust the offset, against 'offset' and 'size' options. */
static inline int raw_adjust_offset(BlockDriverState *bs, uint64_t *offset,
                                    uint64_t bytes, bool is_write)
{
    BDRVRawState *s = bs->opaque;

    if (s->has_size && (*offset > s->size || bytes > (s->size - *offset))) {
        /* There's not enough space for the write, or the read request is
         * out-of-range. Don't read/write anything to prevent leaking out of
         * the size specified in options. */
        return is_write ? -ENOSPC : -EINVAL;
    }

    if (*offset > INT64_MAX - s->offset) {
        return -EINVAL;
    }
    *offset += s->offset;

    return 0;
}

static int coroutine_fn raw_co_preadv(BlockDriverState *bs, uint64_t offset,
                                      uint64_t bytes, QEMUIOVector *qiov,
                                      int flags)
{
    int ret;

    ret = raw_adjust_offset(bs, &offset, bytes, false);
    if (ret) {
        return ret;
    }

    BLKDBG_EVENT(bs->file, BLKDBG_READ_AIO);
    return bdrv_co_preadv(bs->file, offset, bytes, qiov, flags);
}

static int coroutine_fn raw_co_pwritev(BlockDriverState *bs, uint64_t offset,
                                       uint64_t bytes, QEMUIOVector *qiov,
                                       int flags)
{
    void *buf = NULL;
    BlockDriver *drv;
    QEMUIOVector local_qiov;
    int ret;

    if (bs->probed && offset < BLOCK_PROBE_BUF_SIZE && bytes) {
        /* Handling partial writes would be a pain - so we just
         * require that guests have 512-byte request alignment if
         * probing occurred */
        QEMU_BUILD_BUG_ON(BLOCK_PROBE_BUF_SIZE != 512);
        QEMU_BUILD_BUG_ON(BDRV_SECTOR_SIZE != 512);
        assert(offset == 0 && bytes >= BLOCK_PROBE_BUF_SIZE);

        buf = qemu_try_blockalign(bs->file->bs, 512);
        if (!buf) {
            ret = -ENOMEM;
            goto fail;
        }

        ret = qemu_iovec_to_buf(qiov, 0, buf, 512);
        if (ret != 512) {
            ret = -EINVAL;
            goto fail;
        }

        drv = bdrv_probe_all(buf, 512, NULL);
        if (drv != bs->drv) {
            ret = -EPERM;
            goto fail;
        }

        /* Use the checked buffer, a malicious guest might be overwriting its
         * original buffer in the background. */
        qemu_iovec_init(&local_qiov, qiov->niov + 1);
        qemu_iovec_add(&local_qiov, buf, 512);
        qemu_iovec_concat(&local_qiov, qiov, 512, qiov->size - 512);
        qiov = &local_qiov;
    }

    ret = raw_adjust_offset(bs, &offset, bytes, true);
    if (ret) {
        goto fail;
    }

    BLKDBG_EVENT(bs->file, BLKDBG_WRITE_AIO);
    ret = bdrv_co_pwritev(bs->file, offset, bytes, qiov, flags);

fail:
    if (qiov == &local_qiov) {
        qemu_iovec_destroy(&local_qiov);
    }
    qemu_vfree(buf);
    return ret;
}

static int coroutine_fn raw_co_block_status(BlockDriverState *bs,
                                            bool want_zero, int64_t offset,
                                            int64_t bytes, int64_t *pnum,
                                            int64_t *map,
                                            BlockDriverState **file)
{
    BDRVRawState *s = bs->opaque;
    *pnum = bytes;
    *file = bs->file->bs;
    *map = offset + s->offset;
    return BDRV_BLOCK_RAW | BDRV_BLOCK_OFFSET_VALID;
}

static int coroutine_fn raw_co_pwrite_zeroes(BlockDriverState *bs,
                                             int64_t offset, int bytes,
                                             BdrvRequestFlags flags)
{
    int ret;

    ret = raw_adjust_offset(bs, (uint64_t *)&offset, bytes, true);
    if (ret) {
        return ret;
    }
    return bdrv_co_pwrite_zeroes(bs->file, offset, bytes, flags);
}

static int coroutine_fn raw_co_pdiscard(BlockDriverState *bs,
                                        int64_t offset, int bytes)
{
    int ret;

    ret = raw_adjust_offset(bs, (uint64_t *)&offset, bytes, true);
    if (ret) {
        return ret;
    }
    return bdrv_co_pdiscard(bs->file, offset, bytes);
}

static int64_t raw_getlength(BlockDriverState *bs)
{
    int64_t len;
    BDRVRawState *s = bs->opaque;

    /* Update size. It should not change unless the file was externally
     * modified. */
    len = bdrv_getlength(bs->file->bs);
    if (len < 0) {
        return len;
    }

    if (len < s->offset) {
        s->size = 0;
    } else {
        if (s->has_size) {
            /* Try to honour the size */
            s->size = MIN(s->size, len - s->offset);
        } else {
            s->size = len - s->offset;
        }
    }

    return s->size;
}

static BlockMeasureInfo *raw_measure(QemuOpts *opts, BlockDriverState *in_bs,
                                     Error **errp)
{
    BlockMeasureInfo *info;
    int64_t required;

    if (in_bs) {
        required = bdrv_getlength(in_bs);
        if (required < 0) {
            error_setg_errno(errp, -required, "Unable to get image size");
            return NULL;
        }
    } else {
        required = ROUND_UP(qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0),
                            BDRV_SECTOR_SIZE);
    }

    info = g_new(BlockMeasureInfo, 1);
    info->required = required;

    /* Unallocated sectors count towards the file size in raw images */
    info->fully_allocated = info->required;
    return info;
}

static int raw_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    return bdrv_get_info(bs->file->bs, bdi);
}

static void raw_refresh_limits(BlockDriverState *bs, Error **errp)
{
    if (bs->probed) {
        /* To make it easier to protect the first sector, any probed
         * image is restricted to read-modify-write on sub-sector
         * operations. */
        bs->bl.request_alignment = BDRV_SECTOR_SIZE;
    }
}

static int coroutine_fn raw_co_truncate(BlockDriverState *bs, int64_t offset,
                                        bool exact, PreallocMode prealloc,
                                        Error **errp)
{
    BDRVRawState *s = bs->opaque;

    if (s->has_size) {
        error_setg(errp, "Cannot resize fixed-size raw disks");
        return -ENOTSUP;
    }

    if (INT64_MAX - offset < s->offset) {
        error_setg(errp, "Disk size too large for the chosen offset");
        return -EINVAL;
    }

    s->size = offset;
    offset += s->offset;
    return bdrv_co_truncate(bs->file, offset, exact, prealloc, errp);
}

static void raw_eject(BlockDriverState *bs, bool eject_flag)
{
    bdrv_eject(bs->file->bs, eject_flag);
}

static void raw_lock_medium(BlockDriverState *bs, bool locked)
{
    bdrv_lock_medium(bs->file->bs, locked);
}

static int raw_co_ioctl(BlockDriverState *bs, unsigned long int req, void *buf)
{
    BDRVRawState *s = bs->opaque;
    if (s->offset || s->has_size) {
        return -ENOTSUP;
    }
    return bdrv_co_ioctl(bs->file->bs, req, buf);
}

static int raw_has_zero_init(BlockDriverState *bs)
{
    return bdrv_has_zero_init(bs->file->bs);
}

static int raw_has_zero_init_truncate(BlockDriverState *bs)
{
    return bdrv_has_zero_init_truncate(bs->file->bs);
}

static int coroutine_fn raw_co_create_opts(const char *filename, QemuOpts *opts,
                                           Error **errp)
{
    return bdrv_create_file(filename, opts, errp);
}

static int raw_open(BlockDriverState *bs, QDict *options, int flags,
                    Error **errp)
{
    BDRVRawState *s = bs->opaque;
    int ret;

    bs->file = bdrv_open_child(NULL, options, "file", bs, &child_file,
                               false, errp);
    if (!bs->file) {
        return -EINVAL;
    }

    bs->sg = bs->file->bs->sg;
    bs->supported_write_flags = BDRV_REQ_WRITE_UNCHANGED |
        (BDRV_REQ_FUA & bs->file->bs->supported_write_flags);
    bs->supported_zero_flags = BDRV_REQ_WRITE_UNCHANGED |
        ((BDRV_REQ_FUA | BDRV_REQ_MAY_UNMAP | BDRV_REQ_NO_FALLBACK) &
            bs->file->bs->supported_zero_flags);

    if (bs->probed && !bdrv_is_read_only(bs)) {
        bdrv_refresh_filename(bs->file->bs);
        fprintf(stderr,
                "WARNING: Image format was not specified for '%s' and probing "
                "guessed raw.\n"
                "         Automatically detecting the format is dangerous for "
                "raw images, write operations on block 0 will be restricted.\n"
                "         Specify the 'raw' format explicitly to remove the "
                "restrictions.\n",
                bs->file->bs->filename);
    }

    ret = raw_read_options(options, bs, s, errp);
    if (ret < 0) {
        return ret;
    }

    if (bs->sg && (s->offset || s->has_size)) {
        error_setg(errp, "Cannot use offset/size with SCSI generic devices");
        return -EINVAL;
    }

    return 0;
}

static int raw_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    /* smallest possible positive score so that raw is used if and only if no
     * other block driver works
     */
    return 1;
}

static int raw_probe_blocksizes(BlockDriverState *bs, BlockSizes *bsz)
{
    BDRVRawState *s = bs->opaque;
    int ret;

    ret = bdrv_probe_blocksizes(bs->file->bs, bsz);
    if (ret < 0) {
        return ret;
    }

    if (!QEMU_IS_ALIGNED(s->offset, MAX(bsz->log, bsz->phys))) {
        return -ENOTSUP;
    }

    return 0;
}

static int raw_probe_geometry(BlockDriverState *bs, HDGeometry *geo)
{
    BDRVRawState *s = bs->opaque;
    if (s->offset || s->has_size) {
        return -ENOTSUP;
    }
    return bdrv_probe_geometry(bs->file->bs, geo);
}

static int coroutine_fn raw_co_copy_range_from(BlockDriverState *bs,
                                               BdrvChild *src,
                                               uint64_t src_offset,
                                               BdrvChild *dst,
                                               uint64_t dst_offset,
                                               uint64_t bytes,
                                               BdrvRequestFlags read_flags,
                                               BdrvRequestFlags write_flags)
{
    int ret;

    ret = raw_adjust_offset(bs, &src_offset, bytes, false);
    if (ret) {
        return ret;
    }
    return bdrv_co_copy_range_from(bs->file, src_offset, dst, dst_offset,
                                   bytes, read_flags, write_flags);
}

static int coroutine_fn raw_co_copy_range_to(BlockDriverState *bs,
                                             BdrvChild *src,
                                             uint64_t src_offset,
                                             BdrvChild *dst,
                                             uint64_t dst_offset,
                                             uint64_t bytes,
                                             BdrvRequestFlags read_flags,
                                             BdrvRequestFlags write_flags)
{
    int ret;

    ret = raw_adjust_offset(bs, &dst_offset, bytes, true);
    if (ret) {
        return ret;
    }
    return bdrv_co_copy_range_to(src, src_offset, bs->file, dst_offset, bytes,
                                 read_flags, write_flags);
}

static const char *const raw_strong_runtime_opts[] = {
    "offset",
    "size",

    NULL
};

BlockDriver bdrv_raw = {
    .format_name          = "raw",
    .instance_size        = sizeof(BDRVRawState),
    .bdrv_probe           = &raw_probe,
    .bdrv_reopen_prepare  = &raw_reopen_prepare,
    .bdrv_reopen_commit   = &raw_reopen_commit,
    .bdrv_reopen_abort    = &raw_reopen_abort,
    .bdrv_open            = &raw_open,
    .bdrv_child_perm      = bdrv_filter_default_perms,
    .bdrv_co_create_opts  = &raw_co_create_opts,
    .bdrv_co_preadv       = &raw_co_preadv,
    .bdrv_co_pwritev      = &raw_co_pwritev,
    .bdrv_co_pwrite_zeroes = &raw_co_pwrite_zeroes,
    .bdrv_co_pdiscard     = &raw_co_pdiscard,
    .bdrv_co_block_status = &raw_co_block_status,
    .bdrv_co_copy_range_from = &raw_co_copy_range_from,
    .bdrv_co_copy_range_to  = &raw_co_copy_range_to,
    .bdrv_co_truncate     = &raw_co_truncate,
    .bdrv_getlength       = &raw_getlength,
    .has_variable_length  = true,
    .bdrv_measure         = &raw_measure,
    .bdrv_get_info        = &raw_get_info,
    .bdrv_refresh_limits  = &raw_refresh_limits,
    .bdrv_probe_blocksizes = &raw_probe_blocksizes,
    .bdrv_probe_geometry  = &raw_probe_geometry,
    .bdrv_eject           = &raw_eject,
    .bdrv_lock_medium     = &raw_lock_medium,
    .bdrv_co_ioctl        = &raw_co_ioctl,
    .create_opts          = &raw_create_opts,
    .bdrv_has_zero_init   = &raw_has_zero_init,
    .bdrv_has_zero_init_truncate = &raw_has_zero_init_truncate,
    .strong_runtime_opts  = raw_strong_runtime_opts,
    .mutable_opts         = mutable_opts,
};

static void bdrv_raw_init(void)
{
    bdrv_register(&bdrv_raw);
}

block_init(bdrv_raw_init);
