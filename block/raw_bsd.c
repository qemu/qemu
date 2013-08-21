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

static const QEMUOptionParameter raw_create_options[] = {
    {
        .name = BLOCK_OPT_SIZE,
        .type = OPT_SIZE,
        .help = "Virtual disk size"
    },
    { 0 }
};

static TYPE raw_reopen_prepare(BlockDriverState *bs)
{
    return bdrv_reopen_prepare(bs->file);
}

static TYPE raw_co_readv(BlockDriverState *bs)
{
    BLKDBG_EVENT(bs->file, BLKDBG_READ_AIO);
    return bdrv_co_readv(bs->file);
}

static TYPE raw_co_writev(BlockDriverState *bs)
{
    BLKDBG_EVENT(bs->file, BLKDBG_WRITE_AIO);
    return bdrv_co_writev(bs->file);
}

static TYPE raw_co_is_allocated(BlockDriverState *bs)
{
    return bdrv_co_is_allocated(bs->file);
}

static TYPE raw_co_write_zeroes(BlockDriverState *bs)
{
    return bdrv_co_write_zeroes(bs->file);
}

static TYPE raw_co_discard(BlockDriverState *bs)
{
    return bdrv_co_discard(bs->file);
}

static TYPE raw_getlength(BlockDriverState *bs)
{
    return bdrv_getlength(bs->file);
}

static TYPE raw_get_info(BlockDriverState *bs)
{
    return bdrv_get_info(bs->file);
}

static TYPE raw_truncate(BlockDriverState *bs)
{
    return bdrv_truncate(bs->file);
}

static TYPE raw_is_inserted(BlockDriverState *bs)
{
    return bdrv_is_inserted(bs->file);
}

static TYPE raw_media_changed(BlockDriverState *bs)
{
    return bdrv_media_changed(bs->file);
}

static TYPE raw_eject(BlockDriverState *bs)
{
    return bdrv_eject(bs->file);
}

static TYPE raw_lock_medium(BlockDriverState *bs)
{
    return bdrv_lock_medium(bs->file);
}

static TYPE raw_ioctl(BlockDriverState *bs)
{
    return bdrv_ioctl(bs->file);
}

static TYPE raw_aio_ioctl(BlockDriverState *bs)
{
    return bdrv_aio_ioctl(bs->file);
}

static TYPE raw_has_zero_init(BlockDriverState *bs)
{
    return bdrv_has_zero_init(bs->file);
}

static TYPE raw_create(void)
{
    return bdrv_create_file();
}

static int raw_open(BlockDriverState *bs)
{
    bs->sg = bs->file->sg;
    return 0;
}

static void raw_close(void)
{
}

static int raw_probe(void)
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
    .bdrv_co_is_allocated = &raw_co_is_allocated,
    .bdrv_truncate        = &raw_truncate,
    .bdrv_getlength       = &raw_getlength,
    .bdrv_get_info        = &raw_get_info,
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
