/* BlockDriver implementation for "raw"
 *
 * Copyright (C) 2010, 2013, Red Hat, Inc.
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
#include "qemu/option.h"

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

static int raw_reopen_prepare(BDRVReopenState *reopen_state,
                              BlockReopenQueue *queue, Error **errp)
{
    return 0;
}

static int coroutine_fn raw_co_readv(BlockDriverState *bs, int64_t sector_num,
                                     int nb_sectors, QEMUIOVector *qiov)
{
    BLKDBG_EVENT(bs->file, BLKDBG_READ_AIO);
    return bdrv_co_readv(bs->file->bs, sector_num, nb_sectors, qiov);
}

static int coroutine_fn
raw_co_writev_flags(BlockDriverState *bs, int64_t sector_num, int nb_sectors,
                    QEMUIOVector *qiov, int flags)
{
    void *buf = NULL;
    BlockDriver *drv;
    QEMUIOVector local_qiov;
    int ret;

    if (bs->probed && sector_num == 0) {
        /* As long as these conditions are true, we can't get partial writes to
         * the probe buffer and can just directly check the request. */
        QEMU_BUILD_BUG_ON(BLOCK_PROBE_BUF_SIZE != 512);
        QEMU_BUILD_BUG_ON(BDRV_SECTOR_SIZE != 512);

        if (nb_sectors == 0) {
            /* qemu_iovec_to_buf() would fail, but we want to return success
             * instead of -EINVAL in this case. */
            return 0;
        }

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

    BLKDBG_EVENT(bs->file, BLKDBG_WRITE_AIO);
    ret = bdrv_co_pwritev(bs->file->bs, sector_num * BDRV_SECTOR_SIZE,
                          nb_sectors * BDRV_SECTOR_SIZE, qiov, flags);

fail:
    if (qiov == &local_qiov) {
        qemu_iovec_destroy(&local_qiov);
    }
    qemu_vfree(buf);
    return ret;
}

static int64_t coroutine_fn raw_co_get_block_status(BlockDriverState *bs,
                                            int64_t sector_num,
                                            int nb_sectors, int *pnum,
                                            BlockDriverState **file)
{
    *pnum = nb_sectors;
    *file = bs->file->bs;
    return BDRV_BLOCK_RAW | BDRV_BLOCK_OFFSET_VALID | BDRV_BLOCK_DATA |
           (sector_num << BDRV_SECTOR_BITS);
}

static int coroutine_fn raw_co_write_zeroes(BlockDriverState *bs,
                                            int64_t sector_num, int nb_sectors,
                                            BdrvRequestFlags flags)
{
    return bdrv_co_write_zeroes(bs->file->bs, sector_num, nb_sectors, flags);
}

static int coroutine_fn raw_co_discard(BlockDriverState *bs,
                                       int64_t sector_num, int nb_sectors)
{
    return bdrv_co_discard(bs->file->bs, sector_num, nb_sectors);
}

static int64_t raw_getlength(BlockDriverState *bs)
{
    return bdrv_getlength(bs->file->bs);
}

static int raw_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    return bdrv_get_info(bs->file->bs, bdi);
}

static void raw_refresh_limits(BlockDriverState *bs, Error **errp)
{
    bs->bl = bs->file->bs->bl;
}

static int raw_truncate(BlockDriverState *bs, int64_t offset)
{
    return bdrv_truncate(bs->file->bs, offset);
}

static int raw_media_changed(BlockDriverState *bs)
{
    return bdrv_media_changed(bs->file->bs);
}

static void raw_eject(BlockDriverState *bs, bool eject_flag)
{
    bdrv_eject(bs->file->bs, eject_flag);
}

static void raw_lock_medium(BlockDriverState *bs, bool locked)
{
    bdrv_lock_medium(bs->file->bs, locked);
}

static BlockAIOCB *raw_aio_ioctl(BlockDriverState *bs,
                                 unsigned long int req, void *buf,
                                 BlockCompletionFunc *cb,
                                 void *opaque)
{
    return bdrv_aio_ioctl(bs->file->bs, req, buf, cb, opaque);
}

static int raw_has_zero_init(BlockDriverState *bs)
{
    return bdrv_has_zero_init(bs->file->bs);
}

static int raw_create(const char *filename, QemuOpts *opts, Error **errp)
{
    Error *local_err = NULL;
    int ret;

    ret = bdrv_create_file(filename, opts, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
    }
    return ret;
}

static int raw_open(BlockDriverState *bs, QDict *options, int flags,
                    Error **errp)
{
    bs->sg = bs->file->bs->sg;
    bs->supported_write_flags = BDRV_REQ_FUA;
    bs->supported_zero_flags = BDRV_REQ_FUA | BDRV_REQ_MAY_UNMAP;

    if (bs->probed && !bdrv_is_read_only(bs)) {
        fprintf(stderr,
                "WARNING: Image format was not specified for '%s' and probing "
                "guessed raw.\n"
                "         Automatically detecting the format is dangerous for "
                "raw images, write operations on block 0 will be restricted.\n"
                "         Specify the 'raw' format explicitly to remove the "
                "restrictions.\n",
                bs->file->bs->filename);
    }

    return 0;
}

static void raw_close(BlockDriverState *bs)
{
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
    return bdrv_probe_blocksizes(bs->file->bs, bsz);
}

static int raw_probe_geometry(BlockDriverState *bs, HDGeometry *geo)
{
    return bdrv_probe_geometry(bs->file->bs, geo);
}

BlockDriver bdrv_raw = {
    .format_name          = "raw",
    .bdrv_probe           = &raw_probe,
    .bdrv_reopen_prepare  = &raw_reopen_prepare,
    .bdrv_open            = &raw_open,
    .bdrv_close           = &raw_close,
    .bdrv_create          = &raw_create,
    .bdrv_co_readv        = &raw_co_readv,
    .bdrv_co_writev_flags = &raw_co_writev_flags,
    .bdrv_co_write_zeroes = &raw_co_write_zeroes,
    .bdrv_co_discard      = &raw_co_discard,
    .bdrv_co_get_block_status = &raw_co_get_block_status,
    .bdrv_truncate        = &raw_truncate,
    .bdrv_getlength       = &raw_getlength,
    .has_variable_length  = true,
    .bdrv_get_info        = &raw_get_info,
    .bdrv_refresh_limits  = &raw_refresh_limits,
    .bdrv_probe_blocksizes = &raw_probe_blocksizes,
    .bdrv_probe_geometry  = &raw_probe_geometry,
    .bdrv_media_changed   = &raw_media_changed,
    .bdrv_eject           = &raw_eject,
    .bdrv_lock_medium     = &raw_lock_medium,
    .bdrv_aio_ioctl       = &raw_aio_ioctl,
    .create_opts          = &raw_create_opts,
    .bdrv_has_zero_init   = &raw_has_zero_init
};

static void bdrv_raw_init(void)
{
    bdrv_register(&bdrv_raw);
}

block_init(bdrv_raw_init);
