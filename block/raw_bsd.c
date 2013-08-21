/* BlockDriver implementation for "raw"
 *
 * Copyright (C) 2013, Red Hat, Inc.
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

static TYPE raw_reopen_prepare(BlockDriverState *bs)
{
    return bdrv_reopen_prepare(bs->file);
}

static TYPE raw_co_readv(BlockDriverState *bs)
{
    return bdrv_co_readv(bs->file);
}

static TYPE raw_co_writev(BlockDriverState *bs)
{
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

