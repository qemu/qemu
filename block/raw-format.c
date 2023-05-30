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
#include "block/block-io.h"
#include "block/block_int.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/memalign.h"

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

static int raw_read_options(QDict *options, uint64_t *offset, bool *has_size,
                            uint64_t *size, Error **errp)
{
    QemuOpts *opts = NULL;
    int ret;

    opts = qemu_opts_create(&raw_runtime_opts, NULL, 0, &error_abort);
    if (!qemu_opts_absorb_qdict(opts, options, errp)) {
        ret = -EINVAL;
        goto end;
    }

    *offset = qemu_opt_get_size(opts, "offset", 0);
    *has_size = qemu_opt_find(opts, "size");
    *size = qemu_opt_get_size(opts, "size", 0);

    ret = 0;
end:
    qemu_opts_del(opts);
    return ret;
}

static int raw_apply_options(BlockDriverState *bs, BDRVRawState *s,
                             uint64_t offset, bool has_size, uint64_t size,
                             Error **errp)
{
    int64_t real_size = 0;

    real_size = bdrv_getlength(bs->file->bs);
    if (real_size < 0) {
        error_setg_errno(errp, -real_size, "Could not get image size");
        return real_size;
    }

    /* Check size and offset */
    if (offset > real_size) {
        error_setg(errp, "Offset (%" PRIu64 ") cannot be greater than "
                   "size of the containing file (%" PRId64 ")",
                   s->offset, real_size);
        return -EINVAL;
    }

    if (has_size && (real_size - offset) < size) {
        error_setg(errp, "The sum of offset (%" PRIu64 ") and size "
                   "(%" PRIu64 ") has to be smaller or equal to the "
                   " actual size of the containing file (%" PRId64 ")",
                   s->offset, s->size, real_size);
        return -EINVAL;
    }

    /* Make sure size is multiple of BDRV_SECTOR_SIZE to prevent rounding
     * up and leaking out of the specified area. */
    if (has_size && !QEMU_IS_ALIGNED(size, BDRV_SECTOR_SIZE)) {
        error_setg(errp, "Specified size is not multiple of %llu",
                   BDRV_SECTOR_SIZE);
        return -EINVAL;
    }

    s->offset = offset;
    s->has_size = has_size;
    s->size = has_size ? size : real_size - offset;

    return 0;
}

static int raw_reopen_prepare(BDRVReopenState *reopen_state,
                              BlockReopenQueue *queue, Error **errp)
{
    bool has_size;
    uint64_t offset, size;
    int ret;

    assert(reopen_state != NULL);
    assert(reopen_state->bs != NULL);

    reopen_state->opaque = g_new0(BDRVRawState, 1);

    ret = raw_read_options(reopen_state->options, &offset, &has_size, &size,
                           errp);
    if (ret < 0) {
        return ret;
    }

    ret = raw_apply_options(reopen_state->bs, reopen_state->opaque,
                            offset, has_size, size, errp);
    if (ret < 0) {
        return ret;
    }

    return 0;
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
static inline int raw_adjust_offset(BlockDriverState *bs, int64_t *offset,
                                    int64_t bytes, bool is_write)
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

static int coroutine_fn GRAPH_RDLOCK
raw_co_preadv(BlockDriverState *bs, int64_t offset, int64_t bytes,
              QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    int ret;

    ret = raw_adjust_offset(bs, &offset, bytes, false);
    if (ret) {
        return ret;
    }

    BLKDBG_EVENT(bs->file, BLKDBG_READ_AIO);
    return bdrv_co_preadv(bs->file, offset, bytes, qiov, flags);
}

static int coroutine_fn GRAPH_RDLOCK
raw_co_pwritev(BlockDriverState *bs, int64_t offset, int64_t bytes,
               QEMUIOVector *qiov, BdrvRequestFlags flags)
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

        flags &= ~BDRV_REQ_REGISTERED_BUF;
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

static int coroutine_fn GRAPH_RDLOCK
raw_co_pwrite_zeroes(BlockDriverState *bs, int64_t offset, int64_t bytes,
                     BdrvRequestFlags flags)
{
    int ret;

    ret = raw_adjust_offset(bs, &offset, bytes, true);
    if (ret) {
        return ret;
    }
    return bdrv_co_pwrite_zeroes(bs->file, offset, bytes, flags);
}

static int coroutine_fn GRAPH_RDLOCK
raw_co_pdiscard(BlockDriverState *bs, int64_t offset, int64_t bytes)
{
    int ret;

    ret = raw_adjust_offset(bs, &offset, bytes, true);
    if (ret) {
        return ret;
    }
    return bdrv_co_pdiscard(bs->file, offset, bytes);
}

static int coroutine_fn GRAPH_RDLOCK
raw_co_zone_report(BlockDriverState *bs, int64_t offset,
                   unsigned int *nr_zones,
                   BlockZoneDescriptor *zones)
{
    return bdrv_co_zone_report(bs->file->bs, offset, nr_zones, zones);
}

static int coroutine_fn GRAPH_RDLOCK
raw_co_zone_mgmt(BlockDriverState *bs, BlockZoneOp op,
                 int64_t offset, int64_t len)
{
    return bdrv_co_zone_mgmt(bs->file->bs, op, offset, len);
}

static int coroutine_fn GRAPH_RDLOCK
raw_co_zone_append(BlockDriverState *bs,int64_t *offset, QEMUIOVector *qiov,
                   BdrvRequestFlags flags)
{
    return bdrv_co_zone_append(bs->file->bs, offset, qiov, flags);
}

static int64_t coroutine_fn GRAPH_RDLOCK
raw_co_getlength(BlockDriverState *bs)
{
    int64_t len;
    BDRVRawState *s = bs->opaque;

    /* Update size. It should not change unless the file was externally
     * modified. */
    len = bdrv_co_getlength(bs->file->bs);
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

    info = g_new0(BlockMeasureInfo, 1);
    info->required = required;

    /* Unallocated sectors count towards the file size in raw images */
    info->fully_allocated = info->required;
    return info;
}

static int coroutine_fn GRAPH_RDLOCK
raw_co_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    return bdrv_co_get_info(bs->file->bs, bdi);
}

static void raw_refresh_limits(BlockDriverState *bs, Error **errp)
{
    bs->bl.has_variable_length = bs->file->bs->bl.has_variable_length;

    if (bs->probed) {
        /* To make it easier to protect the first sector, any probed
         * image is restricted to read-modify-write on sub-sector
         * operations. */
        bs->bl.request_alignment = BDRV_SECTOR_SIZE;
    }
}

static int coroutine_fn GRAPH_RDLOCK
raw_co_truncate(BlockDriverState *bs, int64_t offset, bool exact,
                PreallocMode prealloc, BdrvRequestFlags flags, Error **errp)
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
    return bdrv_co_truncate(bs->file, offset, exact, prealloc, flags, errp);
}

static void coroutine_fn GRAPH_RDLOCK
raw_co_eject(BlockDriverState *bs, bool eject_flag)
{
    bdrv_co_eject(bs->file->bs, eject_flag);
}

static void coroutine_fn GRAPH_RDLOCK
raw_co_lock_medium(BlockDriverState *bs, bool locked)
{
    bdrv_co_lock_medium(bs->file->bs, locked);
}

static int coroutine_fn GRAPH_RDLOCK
raw_co_ioctl(BlockDriverState *bs, unsigned long int req, void *buf)
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

static int coroutine_fn GRAPH_UNLOCKED
raw_co_create_opts(BlockDriver *drv, const char *filename,
                   QemuOpts *opts, Error **errp)
{
    return bdrv_co_create_file(filename, opts, errp);
}

static int raw_open(BlockDriverState *bs, QDict *options, int flags,
                    Error **errp)
{
    BDRVRawState *s = bs->opaque;
    AioContext *ctx;
    bool has_size;
    uint64_t offset, size;
    BdrvChildRole file_role;
    int ret;

    ret = raw_read_options(options, &offset, &has_size, &size, errp);
    if (ret < 0) {
        return ret;
    }

    /*
     * Without offset and a size limit, this driver behaves very much
     * like a filter.  With any such limit, it does not.
     */
    if (offset || has_size) {
        file_role = BDRV_CHILD_DATA | BDRV_CHILD_PRIMARY;
    } else {
        file_role = BDRV_CHILD_FILTERED | BDRV_CHILD_PRIMARY;
    }

    bdrv_open_child(NULL, options, "file", bs, &child_of_bds,
                    file_role, false, errp);
    if (!bs->file) {
        return -EINVAL;
    }

    bs->sg = bdrv_is_sg(bs->file->bs);
    bs->supported_write_flags = BDRV_REQ_WRITE_UNCHANGED |
        (BDRV_REQ_FUA & bs->file->bs->supported_write_flags);
    bs->supported_zero_flags = BDRV_REQ_WRITE_UNCHANGED |
        ((BDRV_REQ_FUA | BDRV_REQ_MAY_UNMAP | BDRV_REQ_NO_FALLBACK) &
            bs->file->bs->supported_zero_flags);
    bs->supported_truncate_flags = bs->file->bs->supported_truncate_flags &
                                   BDRV_REQ_ZERO_WRITE;

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

    ctx = bdrv_get_aio_context(bs);
    aio_context_acquire(ctx);
    ret = raw_apply_options(bs, s, offset, has_size, size, errp);
    aio_context_release(ctx);

    if (ret < 0) {
        return ret;
    }

    if (bdrv_is_sg(bs) && (s->offset || s->has_size)) {
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

static int coroutine_fn GRAPH_RDLOCK
raw_co_copy_range_from(BlockDriverState *bs,
                       BdrvChild *src, int64_t src_offset,
                       BdrvChild *dst, int64_t dst_offset,
                       int64_t bytes, BdrvRequestFlags read_flags,
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

static int coroutine_fn GRAPH_RDLOCK
raw_co_copy_range_to(BlockDriverState *bs,
                     BdrvChild *src, int64_t src_offset,
                     BdrvChild *dst, int64_t dst_offset,
                     int64_t bytes, BdrvRequestFlags read_flags,
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

static void raw_cancel_in_flight(BlockDriverState *bs)
{
    bdrv_cancel_in_flight(bs->file->bs);
}

static void raw_child_perm(BlockDriverState *bs, BdrvChild *c,
                           BdrvChildRole role,
                           BlockReopenQueue *reopen_queue,
                           uint64_t parent_perm, uint64_t parent_shared,
                           uint64_t *nperm, uint64_t *nshared)
{
    bdrv_default_perms(bs, c, role, reopen_queue, parent_perm,
                       parent_shared, nperm, nshared);

    /*
     * bdrv_default_perms() may add WRITE and/or RESIZE (see comment in
     * bdrv_default_perms_for_storage() for an explanation) but we only need
     * them if they are in parent_perm. Drop WRITE and RESIZE whenever possible
     * to avoid permission conflicts.
     */
    *nperm &= ~(BLK_PERM_WRITE | BLK_PERM_RESIZE);
    *nperm |= parent_perm & (BLK_PERM_WRITE | BLK_PERM_RESIZE);
}

BlockDriver bdrv_raw = {
    .format_name          = "raw",
    .instance_size        = sizeof(BDRVRawState),
    .supports_zoned_children = true,
    .bdrv_probe           = &raw_probe,
    .bdrv_reopen_prepare  = &raw_reopen_prepare,
    .bdrv_reopen_commit   = &raw_reopen_commit,
    .bdrv_reopen_abort    = &raw_reopen_abort,
    .bdrv_open            = &raw_open,
    .bdrv_child_perm      = raw_child_perm,
    .bdrv_co_create_opts  = &raw_co_create_opts,
    .bdrv_co_preadv       = &raw_co_preadv,
    .bdrv_co_pwritev      = &raw_co_pwritev,
    .bdrv_co_pwrite_zeroes = &raw_co_pwrite_zeroes,
    .bdrv_co_pdiscard     = &raw_co_pdiscard,
    .bdrv_co_zone_report  = &raw_co_zone_report,
    .bdrv_co_zone_mgmt  = &raw_co_zone_mgmt,
    .bdrv_co_zone_append = &raw_co_zone_append,
    .bdrv_co_block_status = &raw_co_block_status,
    .bdrv_co_copy_range_from = &raw_co_copy_range_from,
    .bdrv_co_copy_range_to  = &raw_co_copy_range_to,
    .bdrv_co_truncate     = &raw_co_truncate,
    .bdrv_co_getlength    = &raw_co_getlength,
    .is_format            = true,
    .bdrv_measure         = &raw_measure,
    .bdrv_co_get_info     = &raw_co_get_info,
    .bdrv_refresh_limits  = &raw_refresh_limits,
    .bdrv_probe_blocksizes = &raw_probe_blocksizes,
    .bdrv_probe_geometry  = &raw_probe_geometry,
    .bdrv_co_eject        = &raw_co_eject,
    .bdrv_co_lock_medium  = &raw_co_lock_medium,
    .bdrv_co_ioctl        = &raw_co_ioctl,
    .create_opts          = &raw_create_opts,
    .bdrv_has_zero_init   = &raw_has_zero_init,
    .strong_runtime_opts  = raw_strong_runtime_opts,
    .mutable_opts         = mutable_opts,
    .bdrv_cancel_in_flight = raw_cancel_in_flight,
};

static void bdrv_raw_init(void)
{
    bdrv_register(&bdrv_raw);
}

block_init(bdrv_raw_init);
