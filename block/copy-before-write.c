/*
 * copy-before-write filter driver
 *
 * The driver performs Copy-Before-Write (CBW) operation: it is injected above
 * some node, and before each write it copies _old_ data to the target node.
 *
 * Copyright (c) 2018-2021 Virtuozzo International GmbH.
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
#include "qapi/error.h"
#include "block/block_int.h"
#include "block/qdict.h"
#include "block/block-copy.h"

#include "block/copy-before-write.h"

typedef struct BDRVCopyBeforeWriteState {
    BlockCopyState *bcs;
    BdrvChild *target;
} BDRVCopyBeforeWriteState;

static coroutine_fn int cbw_co_preadv(
        BlockDriverState *bs, uint64_t offset, uint64_t bytes,
        QEMUIOVector *qiov, int flags)
{
    return bdrv_co_preadv(bs->file, offset, bytes, qiov, flags);
}

static coroutine_fn int cbw_do_copy_before_write(BlockDriverState *bs,
        uint64_t offset, uint64_t bytes, BdrvRequestFlags flags)
{
    BDRVCopyBeforeWriteState *s = bs->opaque;
    uint64_t off, end;
    int64_t cluster_size = block_copy_cluster_size(s->bcs);

    if (flags & BDRV_REQ_WRITE_UNCHANGED) {
        return 0;
    }

    off = QEMU_ALIGN_DOWN(offset, cluster_size);
    end = QEMU_ALIGN_UP(offset + bytes, cluster_size);

    return block_copy(s->bcs, off, end - off, true);
}

static int coroutine_fn cbw_co_pdiscard(BlockDriverState *bs,
                                        int64_t offset, int bytes)
{
    int ret = cbw_do_copy_before_write(bs, offset, bytes, 0);
    if (ret < 0) {
        return ret;
    }

    return bdrv_co_pdiscard(bs->file, offset, bytes);
}

static int coroutine_fn cbw_co_pwrite_zeroes(BlockDriverState *bs,
        int64_t offset, int bytes, BdrvRequestFlags flags)
{
    int ret = cbw_do_copy_before_write(bs, offset, bytes, flags);
    if (ret < 0) {
        return ret;
    }

    return bdrv_co_pwrite_zeroes(bs->file, offset, bytes, flags);
}

static coroutine_fn int cbw_co_pwritev(BlockDriverState *bs,
                                       uint64_t offset,
                                       uint64_t bytes,
                                       QEMUIOVector *qiov, int flags)
{
    int ret = cbw_do_copy_before_write(bs, offset, bytes, flags);
    if (ret < 0) {
        return ret;
    }

    return bdrv_co_pwritev(bs->file, offset, bytes, qiov, flags);
}

static int coroutine_fn cbw_co_flush(BlockDriverState *bs)
{
    if (!bs->file) {
        return 0;
    }

    return bdrv_co_flush(bs->file->bs);
}

static void cbw_refresh_filename(BlockDriverState *bs)
{
    pstrcpy(bs->exact_filename, sizeof(bs->exact_filename),
            bs->file->bs->filename);
}

static void cbw_child_perm(BlockDriverState *bs, BdrvChild *c,
                           BdrvChildRole role,
                           BlockReopenQueue *reopen_queue,
                           uint64_t perm, uint64_t shared,
                           uint64_t *nperm, uint64_t *nshared)
{
    if (!(role & BDRV_CHILD_FILTERED)) {
        /*
         * Target child
         *
         * Share write to target (child_file), to not interfere
         * with guest writes to its disk which may be in target backing chain.
         * Can't resize during a backup block job because we check the size
         * only upfront.
         */
        *nshared = BLK_PERM_ALL & ~BLK_PERM_RESIZE;
        *nperm = BLK_PERM_WRITE;
    } else {
        /* Source child */
        bdrv_default_perms(bs, c, role, reopen_queue,
                           perm, shared, nperm, nshared);

        if (!QLIST_EMPTY(&bs->parents)) {
            if (perm & BLK_PERM_WRITE) {
                *nperm = *nperm | BLK_PERM_CONSISTENT_READ;
            }
            *nshared &= ~(BLK_PERM_WRITE | BLK_PERM_RESIZE);
        }
    }
}

static int cbw_init(BlockDriverState *bs, QDict *options, Error **errp)
{
    BDRVCopyBeforeWriteState *s = bs->opaque;

    bs->file = bdrv_open_child(NULL, options, "file", bs, &child_of_bds,
                               BDRV_CHILD_FILTERED | BDRV_CHILD_PRIMARY,
                               false, errp);
    if (!bs->file) {
        return -EINVAL;
    }

    s->target = bdrv_open_child(NULL, options, "target", bs, &child_of_bds,
                                BDRV_CHILD_DATA, false, errp);
    if (!s->target) {
        return -EINVAL;
    }

    bs->total_sectors = bs->file->bs->total_sectors;
    bs->supported_write_flags = BDRV_REQ_WRITE_UNCHANGED |
            (BDRV_REQ_FUA & bs->file->bs->supported_write_flags);
    bs->supported_zero_flags = BDRV_REQ_WRITE_UNCHANGED |
            ((BDRV_REQ_FUA | BDRV_REQ_MAY_UNMAP | BDRV_REQ_NO_FALLBACK) &
             bs->file->bs->supported_zero_flags);

    s->bcs = block_copy_state_new(bs->file, s->target, false, false, errp);
    if (!s->bcs) {
        error_prepend(errp, "Cannot create block-copy-state: ");
        return -EINVAL;
    }

    return 0;
}

BlockDriver bdrv_cbw_filter = {
    .format_name = "copy-before-write",
    .instance_size = sizeof(BDRVCopyBeforeWriteState),

    .bdrv_co_preadv             = cbw_co_preadv,
    .bdrv_co_pwritev            = cbw_co_pwritev,
    .bdrv_co_pwrite_zeroes      = cbw_co_pwrite_zeroes,
    .bdrv_co_pdiscard           = cbw_co_pdiscard,
    .bdrv_co_flush              = cbw_co_flush,

    .bdrv_refresh_filename      = cbw_refresh_filename,

    .bdrv_child_perm            = cbw_child_perm,

    .is_filter = true,
};

BlockDriverState *bdrv_cbw_append(BlockDriverState *source,
                                  BlockDriverState *target,
                                  const char *filter_node_name,
                                  BlockCopyState **bcs,
                                  Error **errp)
{
    ERRP_GUARD();
    int ret;
    BDRVCopyBeforeWriteState *state;
    BlockDriverState *top;
    QDict *opts;

    assert(source->total_sectors == target->total_sectors);

    top = bdrv_new_open_driver(&bdrv_cbw_filter, filter_node_name,
                               BDRV_O_RDWR, errp);
    if (!top) {
        error_prepend(errp, "Cannot open driver: ");
        return NULL;
    }
    state = top->opaque;

    opts = qdict_new();
    qdict_put_str(opts, "file", bdrv_get_node_name(source));
    qdict_put_str(opts, "target", bdrv_get_node_name(target));

    ret = cbw_init(top, opts, errp);
    qobject_unref(opts);
    if (ret < 0) {
        goto fail;
    }

    bdrv_drained_begin(source);
    ret = bdrv_replace_node(source, top, errp);
    bdrv_drained_end(source);
    if (ret < 0) {
        error_prepend(errp, "Cannot append copy-before-write filter: ");
        goto fail;
    }

    *bcs = state->bcs;

    return top;

fail:
    block_copy_state_free(state->bcs);
    bdrv_unref(top);
    return NULL;
}

void bdrv_cbw_drop(BlockDriverState *bs)
{
    BDRVCopyBeforeWriteState *s = bs->opaque;

    bdrv_drop_filter(bs, &error_abort);

    block_copy_state_free(s->bcs);

    bdrv_unref(bs);
}
