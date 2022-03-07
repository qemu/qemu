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
#include "block/reqlist.h"

#include "qapi/qapi-visit-block-core.h"

typedef struct BDRVCopyBeforeWriteState {
    BlockCopyState *bcs;
    BdrvChild *target;

    /*
     * @lock: protects access to @access_bitmap, @done_bitmap and
     * @frozen_read_reqs
     */
    CoMutex lock;

    /*
     * @access_bitmap: represents areas allowed for reading by fleecing user.
     * Reading from non-dirty areas leads to -EACCES.
     */
    BdrvDirtyBitmap *access_bitmap;

    /*
     * @done_bitmap: represents areas that was successfully copied to @target by
     * copy-before-write operations.
     */
    BdrvDirtyBitmap *done_bitmap;

    /*
     * @frozen_read_reqs: current read requests for fleecing user in bs->file
     * node. These areas must not be rewritten by guest.
     */
    BlockReqList frozen_read_reqs;
} BDRVCopyBeforeWriteState;

static coroutine_fn int cbw_co_preadv(
        BlockDriverState *bs, int64_t offset, int64_t bytes,
        QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    return bdrv_co_preadv(bs->file, offset, bytes, qiov, flags);
}

/*
 * Do copy-before-write operation.
 *
 * On failure guest request must be failed too.
 *
 * On success, we also wait for all in-flight fleecing read requests in source
 * node, and it's guaranteed that after cbw_do_copy_before_write() successful
 * return there are no such requests and they will never appear.
 */
static coroutine_fn int cbw_do_copy_before_write(BlockDriverState *bs,
        uint64_t offset, uint64_t bytes, BdrvRequestFlags flags)
{
    BDRVCopyBeforeWriteState *s = bs->opaque;
    int ret;
    uint64_t off, end;
    int64_t cluster_size = block_copy_cluster_size(s->bcs);

    if (flags & BDRV_REQ_WRITE_UNCHANGED) {
        return 0;
    }

    off = QEMU_ALIGN_DOWN(offset, cluster_size);
    end = QEMU_ALIGN_UP(offset + bytes, cluster_size);

    ret = block_copy(s->bcs, off, end - off, true);
    if (ret < 0) {
        return ret;
    }

    WITH_QEMU_LOCK_GUARD(&s->lock) {
        bdrv_set_dirty_bitmap(s->done_bitmap, off, end - off);
        reqlist_wait_all(&s->frozen_read_reqs, off, end - off, &s->lock);
    }

    return 0;
}

static int coroutine_fn cbw_co_pdiscard(BlockDriverState *bs,
                                        int64_t offset, int64_t bytes)
{
    int ret = cbw_do_copy_before_write(bs, offset, bytes, 0);
    if (ret < 0) {
        return ret;
    }

    return bdrv_co_pdiscard(bs->file, offset, bytes);
}

static int coroutine_fn cbw_co_pwrite_zeroes(BlockDriverState *bs,
        int64_t offset, int64_t bytes, BdrvRequestFlags flags)
{
    int ret = cbw_do_copy_before_write(bs, offset, bytes, flags);
    if (ret < 0) {
        return ret;
    }

    return bdrv_co_pwrite_zeroes(bs->file, offset, bytes, flags);
}

static coroutine_fn int cbw_co_pwritev(BlockDriverState *bs,
                                       int64_t offset,
                                       int64_t bytes,
                                       QEMUIOVector *qiov,
                                       BdrvRequestFlags flags)
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

/*
 * If @offset not accessible - return NULL.
 *
 * Otherwise, set @pnum to some bytes that accessible from @file (@file is set
 * to bs->file or to s->target). Return newly allocated BlockReq object that
 * should be than passed to cbw_snapshot_read_unlock().
 *
 * It's guaranteed that guest writes will not interact in the region until
 * cbw_snapshot_read_unlock() called.
 */
static BlockReq *cbw_snapshot_read_lock(BlockDriverState *bs,
                                        int64_t offset, int64_t bytes,
                                        int64_t *pnum, BdrvChild **file)
{
    BDRVCopyBeforeWriteState *s = bs->opaque;
    BlockReq *req = g_new(BlockReq, 1);
    bool done;

    QEMU_LOCK_GUARD(&s->lock);

    if (bdrv_dirty_bitmap_next_zero(s->access_bitmap, offset, bytes) != -1) {
        g_free(req);
        return NULL;
    }

    done = bdrv_dirty_bitmap_status(s->done_bitmap, offset, bytes, pnum);
    if (done) {
        /*
         * Special invalid BlockReq, that is handled in
         * cbw_snapshot_read_unlock(). We don't need to lock something to read
         * from s->target.
         */
        *req = (BlockReq) {.offset = -1, .bytes = -1};
        *file = s->target;
    } else {
        reqlist_init_req(&s->frozen_read_reqs, req, offset, bytes);
        *file = bs->file;
    }

    return req;
}

static void cbw_snapshot_read_unlock(BlockDriverState *bs, BlockReq *req)
{
    BDRVCopyBeforeWriteState *s = bs->opaque;

    if (req->offset == -1 && req->bytes == -1) {
        g_free(req);
        return;
    }

    QEMU_LOCK_GUARD(&s->lock);

    reqlist_remove_req(req);
    g_free(req);
}

static coroutine_fn int
cbw_co_preadv_snapshot(BlockDriverState *bs, int64_t offset, int64_t bytes,
                       QEMUIOVector *qiov, size_t qiov_offset)
{
    BlockReq *req;
    BdrvChild *file;
    int ret;

    /* TODO: upgrade to async loop using AioTask */
    while (bytes) {
        int64_t cur_bytes;

        req = cbw_snapshot_read_lock(bs, offset, bytes, &cur_bytes, &file);
        if (!req) {
            return -EACCES;
        }

        ret = bdrv_co_preadv_part(file, offset, cur_bytes,
                                  qiov, qiov_offset, 0);
        cbw_snapshot_read_unlock(bs, req);
        if (ret < 0) {
            return ret;
        }

        bytes -= cur_bytes;
        offset += cur_bytes;
        qiov_offset += cur_bytes;
    }

    return 0;
}

static int coroutine_fn
cbw_co_snapshot_block_status(BlockDriverState *bs,
                             bool want_zero, int64_t offset, int64_t bytes,
                             int64_t *pnum, int64_t *map,
                             BlockDriverState **file)
{
    BDRVCopyBeforeWriteState *s = bs->opaque;
    BlockReq *req;
    int ret;
    int64_t cur_bytes;
    BdrvChild *child;

    req = cbw_snapshot_read_lock(bs, offset, bytes, &cur_bytes, &child);
    if (!req) {
        return -EACCES;
    }

    ret = bdrv_block_status(child->bs, offset, cur_bytes, pnum, map, file);
    if (child == s->target) {
        /*
         * We refer to s->target only for areas that we've written to it.
         * And we can not report unallocated blocks in s->target: this will
         * break generic block-status-above logic, that will go to
         * copy-before-write filtered child in this case.
         */
        assert(ret & BDRV_BLOCK_ALLOCATED);
    }

    cbw_snapshot_read_unlock(bs, req);

    return ret;
}

static int coroutine_fn cbw_co_pdiscard_snapshot(BlockDriverState *bs,
                                                 int64_t offset, int64_t bytes)
{
    BDRVCopyBeforeWriteState *s = bs->opaque;

    WITH_QEMU_LOCK_GUARD(&s->lock) {
        bdrv_reset_dirty_bitmap(s->access_bitmap, offset, bytes);
    }

    block_copy_reset(s->bcs, offset, bytes);

    return bdrv_co_pdiscard(s->target, offset, bytes);
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

static bool cbw_parse_bitmap_option(QDict *options, BdrvDirtyBitmap **bitmap,
                                    Error **errp)
{
    QDict *bitmap_qdict = NULL;
    BlockDirtyBitmap *bmp_param = NULL;
    Visitor *v = NULL;
    bool ret = false;

    *bitmap = NULL;

    qdict_extract_subqdict(options, &bitmap_qdict, "bitmap.");
    if (!qdict_size(bitmap_qdict)) {
        ret = true;
        goto out;
    }

    v = qobject_input_visitor_new_flat_confused(bitmap_qdict, errp);
    if (!v) {
        goto out;
    }

    visit_type_BlockDirtyBitmap(v, NULL, &bmp_param, errp);
    if (!bmp_param) {
        goto out;
    }

    *bitmap = block_dirty_bitmap_lookup(bmp_param->node, bmp_param->name, NULL,
                                        errp);
    if (!*bitmap) {
        goto out;
    }

    ret = true;

out:
    qapi_free_BlockDirtyBitmap(bmp_param);
    visit_free(v);
    qobject_unref(bitmap_qdict);

    return ret;
}

static int cbw_open(BlockDriverState *bs, QDict *options, int flags,
                    Error **errp)
{
    BDRVCopyBeforeWriteState *s = bs->opaque;
    BdrvDirtyBitmap *bitmap = NULL;
    int64_t cluster_size;

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

    if (!cbw_parse_bitmap_option(options, &bitmap, errp)) {
        return -EINVAL;
    }

    bs->total_sectors = bs->file->bs->total_sectors;
    bs->supported_write_flags = BDRV_REQ_WRITE_UNCHANGED |
            (BDRV_REQ_FUA & bs->file->bs->supported_write_flags);
    bs->supported_zero_flags = BDRV_REQ_WRITE_UNCHANGED |
            ((BDRV_REQ_FUA | BDRV_REQ_MAY_UNMAP | BDRV_REQ_NO_FALLBACK) &
             bs->file->bs->supported_zero_flags);

    s->bcs = block_copy_state_new(bs->file, s->target, bitmap, errp);
    if (!s->bcs) {
        error_prepend(errp, "Cannot create block-copy-state: ");
        return -EINVAL;
    }

    cluster_size = block_copy_cluster_size(s->bcs);

    s->done_bitmap = bdrv_create_dirty_bitmap(bs, cluster_size, NULL, errp);
    if (!s->done_bitmap) {
        return -EINVAL;
    }
    bdrv_disable_dirty_bitmap(s->done_bitmap);

    /* s->access_bitmap starts equal to bcs bitmap */
    s->access_bitmap = bdrv_create_dirty_bitmap(bs, cluster_size, NULL, errp);
    if (!s->access_bitmap) {
        return -EINVAL;
    }
    bdrv_disable_dirty_bitmap(s->access_bitmap);
    bdrv_dirty_bitmap_merge_internal(s->access_bitmap,
                                     block_copy_dirty_bitmap(s->bcs), NULL,
                                     true);

    qemu_co_mutex_init(&s->lock);
    QLIST_INIT(&s->frozen_read_reqs);

    return 0;
}

static void cbw_close(BlockDriverState *bs)
{
    BDRVCopyBeforeWriteState *s = bs->opaque;

    bdrv_release_dirty_bitmap(s->access_bitmap);
    bdrv_release_dirty_bitmap(s->done_bitmap);

    block_copy_state_free(s->bcs);
    s->bcs = NULL;
}

BlockDriver bdrv_cbw_filter = {
    .format_name = "copy-before-write",
    .instance_size = sizeof(BDRVCopyBeforeWriteState),

    .bdrv_open                  = cbw_open,
    .bdrv_close                 = cbw_close,

    .bdrv_co_preadv             = cbw_co_preadv,
    .bdrv_co_pwritev            = cbw_co_pwritev,
    .bdrv_co_pwrite_zeroes      = cbw_co_pwrite_zeroes,
    .bdrv_co_pdiscard           = cbw_co_pdiscard,
    .bdrv_co_flush              = cbw_co_flush,

    .bdrv_co_preadv_snapshot       = cbw_co_preadv_snapshot,
    .bdrv_co_pdiscard_snapshot     = cbw_co_pdiscard_snapshot,
    .bdrv_co_snapshot_block_status = cbw_co_snapshot_block_status,

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
    BDRVCopyBeforeWriteState *state;
    BlockDriverState *top;
    QDict *opts;

    assert(source->total_sectors == target->total_sectors);
    GLOBAL_STATE_CODE();

    opts = qdict_new();
    qdict_put_str(opts, "driver", "copy-before-write");
    if (filter_node_name) {
        qdict_put_str(opts, "node-name", filter_node_name);
    }
    qdict_put_str(opts, "file", bdrv_get_node_name(source));
    qdict_put_str(opts, "target", bdrv_get_node_name(target));

    top = bdrv_insert_node(source, opts, BDRV_O_RDWR, errp);
    if (!top) {
        return NULL;
    }

    state = top->opaque;
    *bcs = state->bcs;

    return top;
}

void bdrv_cbw_drop(BlockDriverState *bs)
{
    GLOBAL_STATE_CODE();
    bdrv_drop_filter(bs, &error_abort);
    bdrv_unref(bs);
}

static void cbw_init(void)
{
    bdrv_register(&bdrv_cbw_filter);
}

block_init(cbw_init);
