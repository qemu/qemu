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

#include "block/block_int.h"
#include "qemu/option.h"

static QEMUOptionParameter raw_create_options[] = {
    {
        .name = BLOCK_OPT_SIZE,
        .type = OPT_SIZE,
        .help = "Virtual disk size"
    },
    { 0 }
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
    return bdrv_co_readv(bs->file, sector_num, nb_sectors, qiov);
}

static int coroutine_fn raw_co_writev(BlockDriverState *bs, int64_t sector_num,
                                      int nb_sectors, QEMUIOVector *qiov)
{
    BLKDBG_EVENT(bs->file, BLKDBG_WRITE_AIO);
    return bdrv_co_writev(bs->file, sector_num, nb_sectors, qiov);
}

static int64_t coroutine_fn raw_co_get_block_status(BlockDriverState *bs,
                                            int64_t sector_num,
                                            int nb_sectors, int *pnum)
{
    *pnum = nb_sectors;
    return BDRV_BLOCK_RAW | BDRV_BLOCK_OFFSET_VALID | BDRV_BLOCK_DATA |
           (sector_num << BDRV_SECTOR_BITS);
}

static int coroutine_fn raw_co_write_zeroes(BlockDriverState *bs,
                                            int64_t sector_num, int nb_sectors,
                                            BdrvRequestFlags flags)
{
    return bdrv_co_write_zeroes(bs->file, sector_num, nb_sectors, flags);
}

static int coroutine_fn raw_co_discard(BlockDriverState *bs,
                                       int64_t sector_num, int nb_sectors)
{
    return bdrv_co_discard(bs->file, sector_num, nb_sectors);
}

static int64_t raw_getlength(BlockDriverState *bs)
{
    return bdrv_getlength(bs->file);
}

static int raw_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    return bdrv_get_info(bs->file, bdi);
}

static int raw_refresh_limits(BlockDriverState *bs)
{
    bs->bl = bs->file->bl;
    return 0;
}

static int raw_truncate(BlockDriverState *bs, int64_t offset)
{
    return bdrv_truncate(bs->file, offset);
}

static int raw_is_inserted(BlockDriverState *bs)
{
    return bdrv_is_inserted(bs->file);
}

static int raw_media_changed(BlockDriverState *bs)
{
    return bdrv_media_changed(bs->file);
}

static void raw_eject(BlockDriverState *bs, bool eject_flag)
{
    bdrv_eject(bs->file, eject_flag);
}

static void raw_lock_medium(BlockDriverState *bs, bool locked)
{
    bdrv_lock_medium(bs->file, locked);
}

static int raw_ioctl(BlockDriverState *bs, unsigned long int req, void *buf)
{
    return bdrv_ioctl(bs->file, req, buf);
}

static BlockDriverAIOCB *raw_aio_ioctl(BlockDriverState *bs,
                                       unsigned long int req, void *buf,
                                       BlockDriverCompletionFunc *cb,
                                       void *opaque)
{
    return bdrv_aio_ioctl(bs->file, req, buf, cb, opaque);
}

static int raw_has_zero_init(BlockDriverState *bs)
{
    return bdrv_has_zero_init(bs->file);
}

static int raw_create(const char *filename, QEMUOptionParameter *options,
                      Error **errp)
{
    Error *local_err = NULL;
    int ret;

    ret = bdrv_create_file(filename, options, NULL, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
    }
    return ret;
}

static int raw_open(BlockDriverState *bs, QDict *options, int flags,
                    Error **errp)
{
    bs->sg = bs->file->sg;
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

static BlockDriver bdrv_raw = {
    .format_name          = "raw",
    .bdrv_probe           = &raw_probe,
    .bdrv_reopen_prepare  = &raw_reopen_prepare,
    .bdrv_open            = &raw_open,
    .bdrv_close           = &raw_close,
    .bdrv_create          = &raw_create,
    .bdrv_co_readv        = &raw_co_readv,
    .bdrv_co_writev       = &raw_co_writev,
    .bdrv_co_write_zeroes = &raw_co_write_zeroes,
    .bdrv_co_discard      = &raw_co_discard,
    .bdrv_co_get_block_status = &raw_co_get_block_status,
    .bdrv_truncate        = &raw_truncate,
    .bdrv_getlength       = &raw_getlength,
    .has_variable_length  = true,
    .bdrv_get_info        = &raw_get_info,
    .bdrv_refresh_limits  = &raw_refresh_limits,
    .bdrv_is_inserted     = &raw_is_inserted,
    .bdrv_media_changed   = &raw_media_changed,
    .bdrv_eject           = &raw_eject,
    .bdrv_lock_medium     = &raw_lock_medium,
    .bdrv_ioctl           = &raw_ioctl,
    .bdrv_aio_ioctl       = &raw_aio_ioctl,
    .create_options       = &raw_create_options[0],
    .bdrv_has_zero_init   = &raw_has_zero_init
};

static void bdrv_raw_init(void)
{
    bdrv_register(&bdrv_raw);
}

block_init(bdrv_raw_init);
