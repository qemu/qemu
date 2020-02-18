/*
 * Copy-on-read filter block driver
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * Author:
 *   Max Reitz <mreitz@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
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


static int cor_open(BlockDriverState *bs, QDict *options, int flags,
                    Error **errp)
{
    bs->file = bdrv_open_child(NULL, options, "file", bs, &child_file, false,
                               errp);
    if (!bs->file) {
        return -EINVAL;
    }

    bs->supported_write_flags = BDRV_REQ_WRITE_UNCHANGED |
        (BDRV_REQ_FUA & bs->file->bs->supported_write_flags);

    bs->supported_zero_flags = BDRV_REQ_WRITE_UNCHANGED |
        ((BDRV_REQ_FUA | BDRV_REQ_MAY_UNMAP | BDRV_REQ_NO_FALLBACK) &
            bs->file->bs->supported_zero_flags);

    return 0;
}


#define PERM_PASSTHROUGH (BLK_PERM_CONSISTENT_READ \
                          | BLK_PERM_WRITE \
                          | BLK_PERM_RESIZE)
#define PERM_UNCHANGED (BLK_PERM_ALL & ~PERM_PASSTHROUGH)

static void cor_child_perm(BlockDriverState *bs, BdrvChild *c,
                           const BdrvChildRole *role,
                           BlockReopenQueue *reopen_queue,
                           uint64_t perm, uint64_t shared,
                           uint64_t *nperm, uint64_t *nshared)
{
    *nperm = perm & PERM_PASSTHROUGH;
    *nshared = (shared & PERM_PASSTHROUGH) | PERM_UNCHANGED;

    /* We must not request write permissions for an inactive node, the child
     * cannot provide it. */
    if (!(bs->open_flags & BDRV_O_INACTIVE)) {
        *nperm |= BLK_PERM_WRITE_UNCHANGED;
    }
}


static int64_t cor_getlength(BlockDriverState *bs)
{
    return bdrv_getlength(bs->file->bs);
}


static int coroutine_fn cor_co_preadv(BlockDriverState *bs,
                                      uint64_t offset, uint64_t bytes,
                                      QEMUIOVector *qiov, int flags)
{
    return bdrv_co_preadv(bs->file, offset, bytes, qiov,
                          flags | BDRV_REQ_COPY_ON_READ);
}


static int coroutine_fn cor_co_pwritev(BlockDriverState *bs,
                                       uint64_t offset, uint64_t bytes,
                                       QEMUIOVector *qiov, int flags)
{

    return bdrv_co_pwritev(bs->file, offset, bytes, qiov, flags);
}


static int coroutine_fn cor_co_pwrite_zeroes(BlockDriverState *bs,
                                             int64_t offset, int bytes,
                                             BdrvRequestFlags flags)
{
    return bdrv_co_pwrite_zeroes(bs->file, offset, bytes, flags);
}


static int coroutine_fn cor_co_pdiscard(BlockDriverState *bs,
                                        int64_t offset, int bytes)
{
    return bdrv_co_pdiscard(bs->file, offset, bytes);
}


static void cor_eject(BlockDriverState *bs, bool eject_flag)
{
    bdrv_eject(bs->file->bs, eject_flag);
}


static void cor_lock_medium(BlockDriverState *bs, bool locked)
{
    bdrv_lock_medium(bs->file->bs, locked);
}


static BlockDriver bdrv_copy_on_read = {
    .format_name                        = "copy-on-read",

    .bdrv_open                          = cor_open,
    .bdrv_child_perm                    = cor_child_perm,

    .bdrv_getlength                     = cor_getlength,

    .bdrv_co_preadv                     = cor_co_preadv,
    .bdrv_co_pwritev                    = cor_co_pwritev,
    .bdrv_co_pwrite_zeroes              = cor_co_pwrite_zeroes,
    .bdrv_co_pdiscard                   = cor_co_pdiscard,

    .bdrv_eject                         = cor_eject,
    .bdrv_lock_medium                   = cor_lock_medium,

    .bdrv_co_block_status               = bdrv_co_block_status_from_file,

    .has_variable_length                = true,
    .is_filter                          = true,
};

static void bdrv_copy_on_read_init(void)
{
    bdrv_register(&bdrv_copy_on_read);
}

block_init(bdrv_copy_on_read_init);
