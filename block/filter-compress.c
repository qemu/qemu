/*
 * Compress filter block driver
 *
 * Copyright (c) 2019 Virtuozzo International GmbH
 *
 * Author:
 *   Andrey Shinkevich <andrey.shinkevich@virtuozzo.com>
 *   (based on block/copy-on-read.c by Max Reitz)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) any later version of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "block/block_int.h"
#include "qemu/module.h"
#include "qapi/error.h"


static int compress_open(BlockDriverState *bs, QDict *options, int flags,
                         Error **errp)
{
    int ret = bdrv_open_file_child(NULL, options, "file", bs, errp);
    if (ret < 0) {
        return ret;
    }

    if (!bs->file->bs->drv || !block_driver_can_compress(bs->file->bs->drv)) {
        error_setg(errp,
                   "Compression is not supported for underlying format: %s",
                   bdrv_get_format_name(bs->file->bs) ?: "(no format)");

        return -ENOTSUP;
    }

    bs->supported_write_flags = BDRV_REQ_WRITE_UNCHANGED |
        (BDRV_REQ_FUA & bs->file->bs->supported_write_flags);

    bs->supported_zero_flags = BDRV_REQ_WRITE_UNCHANGED |
        ((BDRV_REQ_FUA | BDRV_REQ_MAY_UNMAP | BDRV_REQ_NO_FALLBACK) &
            bs->file->bs->supported_zero_flags);

    return 0;
}


static int64_t compress_getlength(BlockDriverState *bs)
{
    return bdrv_getlength(bs->file->bs);
}


static int coroutine_fn compress_co_preadv_part(BlockDriverState *bs,
                                                int64_t offset, int64_t bytes,
                                                QEMUIOVector *qiov,
                                                size_t qiov_offset,
                                                BdrvRequestFlags flags)
{
    return bdrv_co_preadv_part(bs->file, offset, bytes, qiov, qiov_offset,
                               flags);
}


static int coroutine_fn compress_co_pwritev_part(BlockDriverState *bs,
                                                 int64_t offset,
                                                 int64_t bytes,
                                                 QEMUIOVector *qiov,
                                                 size_t qiov_offset,
                                                 BdrvRequestFlags flags)
{
    return bdrv_co_pwritev_part(bs->file, offset, bytes, qiov, qiov_offset,
                                flags | BDRV_REQ_WRITE_COMPRESSED);
}


static int coroutine_fn compress_co_pwrite_zeroes(BlockDriverState *bs,
                                                  int64_t offset, int64_t bytes,
                                                  BdrvRequestFlags flags)
{
    return bdrv_co_pwrite_zeroes(bs->file, offset, bytes, flags);
}


static int coroutine_fn compress_co_pdiscard(BlockDriverState *bs,
                                             int64_t offset, int64_t bytes)
{
    return bdrv_co_pdiscard(bs->file, offset, bytes);
}


static void compress_refresh_limits(BlockDriverState *bs, Error **errp)
{
    BlockDriverInfo bdi;
    int ret;

    if (!bs->file) {
        return;
    }

    ret = bdrv_get_info(bs->file->bs, &bdi);
    if (ret < 0 || bdi.cluster_size == 0) {
        return;
    }

    bs->bl.request_alignment = bdi.cluster_size;
}


static void compress_eject(BlockDriverState *bs, bool eject_flag)
{
    bdrv_eject(bs->file->bs, eject_flag);
}


static void compress_lock_medium(BlockDriverState *bs, bool locked)
{
    bdrv_lock_medium(bs->file->bs, locked);
}


static BlockDriver bdrv_compress = {
    .format_name                        = "compress",

    .bdrv_open                          = compress_open,
    .bdrv_child_perm                    = bdrv_default_perms,

    .bdrv_getlength                     = compress_getlength,

    .bdrv_co_preadv_part                = compress_co_preadv_part,
    .bdrv_co_pwritev_part               = compress_co_pwritev_part,
    .bdrv_co_pwrite_zeroes              = compress_co_pwrite_zeroes,
    .bdrv_co_pdiscard                   = compress_co_pdiscard,
    .bdrv_refresh_limits                = compress_refresh_limits,

    .bdrv_eject                         = compress_eject,
    .bdrv_lock_medium                   = compress_lock_medium,

    .has_variable_length                = true,
    .is_filter                          = true,
};

static void bdrv_compress_init(void)
{
    bdrv_register(&bdrv_compress);
}

block_init(bdrv_compress_init);
