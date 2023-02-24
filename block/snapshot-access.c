/*
 * snapshot_access block driver
 *
 * Copyright (c) 2022 Virtuozzo International GmbH.
 *
 * Author:
 *  Sementsov-Ogievskiy Vladimir <vsementsov@virtuozzo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include "sysemu/block-backend.h"
#include "qemu/cutils.h"
#include "block/block_int.h"

static int coroutine_fn GRAPH_RDLOCK
snapshot_access_co_preadv_part(BlockDriverState *bs,
                               int64_t offset, int64_t bytes,
                               QEMUIOVector *qiov, size_t qiov_offset,
                               BdrvRequestFlags flags)
{
    if (flags) {
        return -ENOTSUP;
    }

    return bdrv_co_preadv_snapshot(bs->file, offset, bytes, qiov, qiov_offset);
}

static int coroutine_fn GRAPH_RDLOCK
snapshot_access_co_block_status(BlockDriverState *bs,
                                bool want_zero, int64_t offset,
                                int64_t bytes, int64_t *pnum,
                                int64_t *map, BlockDriverState **file)
{
    return bdrv_co_snapshot_block_status(bs->file->bs, want_zero, offset,
                                         bytes, pnum, map, file);
}

static int coroutine_fn GRAPH_RDLOCK
snapshot_access_co_pdiscard(BlockDriverState *bs, int64_t offset, int64_t bytes)
{
    return bdrv_co_pdiscard_snapshot(bs->file->bs, offset, bytes);
}

static int coroutine_fn
snapshot_access_co_pwrite_zeroes(BlockDriverState *bs,
                                 int64_t offset, int64_t bytes,
                                 BdrvRequestFlags flags)
{
    return -ENOTSUP;
}

static coroutine_fn int
snapshot_access_co_pwritev_part(BlockDriverState *bs,
                                int64_t offset, int64_t bytes,
                                QEMUIOVector *qiov, size_t qiov_offset,
                                BdrvRequestFlags flags)
{
    return -ENOTSUP;
}


static void snapshot_access_refresh_filename(BlockDriverState *bs)
{
    pstrcpy(bs->exact_filename, sizeof(bs->exact_filename),
            bs->file->bs->filename);
}

static int snapshot_access_open(BlockDriverState *bs, QDict *options, int flags,
                                Error **errp)
{
    bdrv_open_child(NULL, options, "file", bs, &child_of_bds,
                    BDRV_CHILD_DATA | BDRV_CHILD_PRIMARY,
                    false, errp);
    if (!bs->file) {
        return -EINVAL;
    }

    bs->total_sectors = bs->file->bs->total_sectors;

    return 0;
}

static void snapshot_access_child_perm(BlockDriverState *bs, BdrvChild *c,
                                BdrvChildRole role,
                                BlockReopenQueue *reopen_queue,
                                uint64_t perm, uint64_t shared,
                                uint64_t *nperm, uint64_t *nshared)
{
    /*
     * Currently, we don't need any permissions. If bs->file provides
     * snapshot-access API, we can use it.
     */
    *nperm = 0;
    *nshared = BLK_PERM_ALL;
}

BlockDriver bdrv_snapshot_access_drv = {
    .format_name = "snapshot-access",

    .bdrv_open                  = snapshot_access_open,

    .bdrv_co_preadv_part        = snapshot_access_co_preadv_part,
    .bdrv_co_pwritev_part       = snapshot_access_co_pwritev_part,
    .bdrv_co_pwrite_zeroes      = snapshot_access_co_pwrite_zeroes,
    .bdrv_co_pdiscard           = snapshot_access_co_pdiscard,
    .bdrv_co_block_status       = snapshot_access_co_block_status,

    .bdrv_refresh_filename      = snapshot_access_refresh_filename,

    .bdrv_child_perm            = snapshot_access_child_perm,
};

static void snapshot_access_init(void)
{
    bdrv_register(&bdrv_snapshot_access_drv);
}

block_init(snapshot_access_init);
