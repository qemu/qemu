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
#include "block/block-io.h"
#include "block/block_int.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "block/copy-on-read.h"


typedef struct BDRVStateCOR {
    BlockDriverState *bottom_bs;
    bool chain_frozen;
} BDRVStateCOR;


static int cor_open(BlockDriverState *bs, QDict *options, int flags,
                    Error **errp)
{
    BlockDriverState *bottom_bs = NULL;
    BDRVStateCOR *state = bs->opaque;
    /* Find a bottom node name, if any */
    const char *bottom_node = qdict_get_try_str(options, "bottom");
    int ret;

    ret = bdrv_open_file_child(NULL, options, "file", bs, errp);
    if (ret < 0) {
        return ret;
    }

    bs->supported_read_flags = BDRV_REQ_PREFETCH;

    bs->supported_write_flags = BDRV_REQ_WRITE_UNCHANGED |
        (BDRV_REQ_FUA & bs->file->bs->supported_write_flags);

    bs->supported_zero_flags = BDRV_REQ_WRITE_UNCHANGED |
        ((BDRV_REQ_FUA | BDRV_REQ_MAY_UNMAP | BDRV_REQ_NO_FALLBACK) &
            bs->file->bs->supported_zero_flags);

    if (bottom_node) {
        bottom_bs = bdrv_find_node(bottom_node);
        if (!bottom_bs) {
            error_setg(errp, "Bottom node '%s' not found", bottom_node);
            qdict_del(options, "bottom");
            return -EINVAL;
        }
        qdict_del(options, "bottom");

        if (!bottom_bs->drv) {
            error_setg(errp, "Bottom node '%s' not opened", bottom_node);
            return -EINVAL;
        }

        if (bottom_bs->drv->is_filter) {
            error_setg(errp, "Bottom node '%s' is a filter", bottom_node);
            return -EINVAL;
        }

        if (bdrv_freeze_backing_chain(bs, bottom_bs, errp) < 0) {
            return -EINVAL;
        }
        state->chain_frozen = true;

        /*
         * We do freeze the chain, so it shouldn't be removed. Still, storing a
         * pointer worth bdrv_ref().
         */
        bdrv_ref(bottom_bs);
    }
    state->bottom_bs = bottom_bs;

    /*
     * We don't need to call bdrv_child_refresh_perms() now as the permissions
     * will be updated later when the filter node gets its parent.
     */

    return 0;
}


#define PERM_PASSTHROUGH (BLK_PERM_CONSISTENT_READ \
                          | BLK_PERM_WRITE \
                          | BLK_PERM_RESIZE)
#define PERM_UNCHANGED (BLK_PERM_ALL & ~PERM_PASSTHROUGH)

static void cor_child_perm(BlockDriverState *bs, BdrvChild *c,
                           BdrvChildRole role,
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


static int64_t coroutine_fn GRAPH_RDLOCK cor_co_getlength(BlockDriverState *bs)
{
    return bdrv_co_getlength(bs->file->bs);
}


static int coroutine_fn GRAPH_RDLOCK
cor_co_preadv_part(BlockDriverState *bs, int64_t offset, int64_t bytes,
                   QEMUIOVector *qiov, size_t qiov_offset,
                   BdrvRequestFlags flags)
{
    int64_t n;
    int local_flags;
    int ret;
    BDRVStateCOR *state = bs->opaque;

    if (!state->bottom_bs) {
        return bdrv_co_preadv_part(bs->file, offset, bytes, qiov, qiov_offset,
                                   flags | BDRV_REQ_COPY_ON_READ);
    }

    while (bytes) {
        local_flags = flags;

        /* In case of failure, try to copy-on-read anyway */
        ret = bdrv_is_allocated(bs->file->bs, offset, bytes, &n);
        if (ret <= 0) {
            ret = bdrv_is_allocated_above(bdrv_backing_chain_next(bs->file->bs),
                                          state->bottom_bs, true, offset,
                                          n, &n);
            if (ret > 0 || ret < 0) {
                local_flags |= BDRV_REQ_COPY_ON_READ;
            }
            /* Finish earlier if the end of a backing file has been reached */
            if (n == 0) {
                break;
            }
        }

        /* Skip if neither read nor write are needed */
        if ((local_flags & (BDRV_REQ_PREFETCH | BDRV_REQ_COPY_ON_READ)) !=
            BDRV_REQ_PREFETCH) {
            ret = bdrv_co_preadv_part(bs->file, offset, n, qiov, qiov_offset,
                                      local_flags);
            if (ret < 0) {
                return ret;
            }
        }

        offset += n;
        qiov_offset += n;
        bytes -= n;
    }

    return 0;
}


static int coroutine_fn GRAPH_RDLOCK
cor_co_pwritev_part(BlockDriverState *bs, int64_t offset, int64_t bytes,
                    QEMUIOVector *qiov, size_t qiov_offset,
                    BdrvRequestFlags flags)
{
    return bdrv_co_pwritev_part(bs->file, offset, bytes, qiov, qiov_offset,
                                flags);
}


static int coroutine_fn GRAPH_RDLOCK
cor_co_pwrite_zeroes(BlockDriverState *bs, int64_t offset, int64_t bytes,
                     BdrvRequestFlags flags)
{
    return bdrv_co_pwrite_zeroes(bs->file, offset, bytes, flags);
}


static int coroutine_fn GRAPH_RDLOCK
cor_co_pdiscard(BlockDriverState *bs, int64_t offset, int64_t bytes)
{
    return bdrv_co_pdiscard(bs->file, offset, bytes);
}


static int coroutine_fn GRAPH_RDLOCK
cor_co_pwritev_compressed(BlockDriverState *bs, int64_t offset, int64_t bytes,
                          QEMUIOVector *qiov)
{
    return bdrv_co_pwritev(bs->file, offset, bytes, qiov,
                           BDRV_REQ_WRITE_COMPRESSED);
}


static void coroutine_fn GRAPH_RDLOCK
cor_co_eject(BlockDriverState *bs, bool eject_flag)
{
    bdrv_co_eject(bs->file->bs, eject_flag);
}


static void coroutine_fn GRAPH_RDLOCK
cor_co_lock_medium(BlockDriverState *bs, bool locked)
{
    bdrv_co_lock_medium(bs->file->bs, locked);
}


static void cor_close(BlockDriverState *bs)
{
    BDRVStateCOR *s = bs->opaque;

    if (s->chain_frozen) {
        s->chain_frozen = false;
        bdrv_unfreeze_backing_chain(bs, s->bottom_bs);
    }

    bdrv_unref(s->bottom_bs);
}


static BlockDriver bdrv_copy_on_read = {
    .format_name                        = "copy-on-read",
    .instance_size                      = sizeof(BDRVStateCOR),

    .bdrv_open                          = cor_open,
    .bdrv_close                         = cor_close,
    .bdrv_child_perm                    = cor_child_perm,

    .bdrv_co_getlength                  = cor_co_getlength,

    .bdrv_co_preadv_part                = cor_co_preadv_part,
    .bdrv_co_pwritev_part               = cor_co_pwritev_part,
    .bdrv_co_pwrite_zeroes              = cor_co_pwrite_zeroes,
    .bdrv_co_pdiscard                   = cor_co_pdiscard,
    .bdrv_co_pwritev_compressed         = cor_co_pwritev_compressed,

    .bdrv_co_eject                      = cor_co_eject,
    .bdrv_co_lock_medium                = cor_co_lock_medium,

    .has_variable_length                = true,
    .is_filter                          = true,
};


void bdrv_cor_filter_drop(BlockDriverState *cor_filter_bs)
{
    BDRVStateCOR *s = cor_filter_bs->opaque;

    /* unfreeze, as otherwise bdrv_replace_node() will fail */
    if (s->chain_frozen) {
        s->chain_frozen = false;
        bdrv_unfreeze_backing_chain(cor_filter_bs, s->bottom_bs);
    }
    bdrv_drop_filter(cor_filter_bs, &error_abort);
    bdrv_unref(cor_filter_bs);
}


static void bdrv_copy_on_read_init(void)
{
    bdrv_register(&bdrv_copy_on_read);
}

block_init(bdrv_copy_on_read_init);
