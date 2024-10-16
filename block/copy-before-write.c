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
#include "qapi/qmp/qjson.h"

#include "sysemu/block-backend.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "block/block_int.h"
#include "block/qdict.h"
#include "block/block-copy.h"
#include "block/dirty-bitmap.h"

#include "block/copy-before-write.h"
#include "block/reqlist.h"

#include "qapi/qapi-visit-block-core.h"

typedef struct BDRVCopyBeforeWriteState {
    BlockCopyState *bcs;
    BdrvChild *target;
    OnCbwError on_cbw_error;
    uint64_t cbw_timeout_ns;
    bool discard_source;

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
     * node. These areas must not be rewritten by guest. There can be multiple
     * overlapping read requests.
     */
    BlockReqList frozen_read_reqs;

    /*
     * @snapshot_error is normally zero. But on first copy-before-write failure
     * when @on_cbw_error == ON_CBW_ERROR_BREAK_SNAPSHOT, @snapshot_error takes
     * value of this error (<0). After that all in-flight and further
     * snapshot-API requests will fail with that error.
     */
    int snapshot_error;
} BDRVCopyBeforeWriteState;

static int coroutine_fn GRAPH_RDLOCK
cbw_co_preadv(BlockDriverState *bs, int64_t offset, int64_t bytes,
              QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    return bdrv_co_preadv(bs->file, offset, bytes, qiov, flags);
}

static void block_copy_cb(void *opaque)
{
    BlockDriverState *bs = opaque;

    bdrv_dec_in_flight(bs);
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

    if (s->snapshot_error) {
        return 0;
    }

    off = QEMU_ALIGN_DOWN(offset, cluster_size);
    end = QEMU_ALIGN_UP(offset + bytes, cluster_size);

    /*
     * Increase in_flight, so that in case of timed-out block-copy, the
     * remaining background block_copy() request (which can't be immediately
     * cancelled by timeout) is presented in bs->in_flight. This way we are
     * sure that on bs close() we'll previously wait for all timed-out but yet
     * running block_copy calls.
     */
    bdrv_inc_in_flight(bs);
    ret = block_copy(s->bcs, off, end - off, true, s->cbw_timeout_ns,
                     block_copy_cb, bs);
    if (ret < 0 && s->on_cbw_error == ON_CBW_ERROR_BREAK_GUEST_WRITE) {
        return ret;
    }

    WITH_QEMU_LOCK_GUARD(&s->lock) {
        if (ret < 0) {
            assert(s->on_cbw_error == ON_CBW_ERROR_BREAK_SNAPSHOT);
            if (!s->snapshot_error) {
                s->snapshot_error = ret;
            }
        } else {
            bdrv_set_dirty_bitmap(s->done_bitmap, off, end - off);
        }
        reqlist_wait_all(&s->frozen_read_reqs, off, end - off, &s->lock);
    }

    return 0;
}

static int coroutine_fn GRAPH_RDLOCK
cbw_co_pdiscard(BlockDriverState *bs, int64_t offset, int64_t bytes)
{
    int ret = cbw_do_copy_before_write(bs, offset, bytes, 0);
    if (ret < 0) {
        return ret;
    }

    return bdrv_co_pdiscard(bs->file, offset, bytes);
}

static int coroutine_fn GRAPH_RDLOCK
cbw_co_pwrite_zeroes(BlockDriverState *bs, int64_t offset, int64_t bytes,
                     BdrvRequestFlags flags)
{
    int ret = cbw_do_copy_before_write(bs, offset, bytes, flags);
    if (ret < 0) {
        return ret;
    }

    return bdrv_co_pwrite_zeroes(bs->file, offset, bytes, flags);
}

static coroutine_fn GRAPH_RDLOCK
int cbw_co_pwritev(BlockDriverState *bs, int64_t offset, int64_t bytes,
                   QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    int ret = cbw_do_copy_before_write(bs, offset, bytes, flags);
    if (ret < 0) {
        return ret;
    }

    return bdrv_co_pwritev(bs->file, offset, bytes, qiov, flags);
}

static int coroutine_fn GRAPH_RDLOCK cbw_co_flush(BlockDriverState *bs)
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
static BlockReq * coroutine_fn GRAPH_RDLOCK
cbw_snapshot_read_lock(BlockDriverState *bs, int64_t offset, int64_t bytes,
                       int64_t *pnum, BdrvChild **file)
{
    BDRVCopyBeforeWriteState *s = bs->opaque;
    BlockReq *req = g_new(BlockReq, 1);
    bool done;

    QEMU_LOCK_GUARD(&s->lock);

    if (s->snapshot_error) {
        g_free(req);
        return NULL;
    }

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

static coroutine_fn void
cbw_snapshot_read_unlock(BlockDriverState *bs, BlockReq *req)
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

static int coroutine_fn GRAPH_RDLOCK
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

static int coroutine_fn GRAPH_RDLOCK
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

    ret = bdrv_co_block_status(child->bs, offset, cur_bytes, pnum, map, file);
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

static int coroutine_fn GRAPH_RDLOCK
cbw_co_pdiscard_snapshot(BlockDriverState *bs, int64_t offset, int64_t bytes)
{
    BDRVCopyBeforeWriteState *s = bs->opaque;
    uint32_t cluster_size = block_copy_cluster_size(s->bcs);
    int64_t aligned_offset = QEMU_ALIGN_UP(offset, cluster_size);
    int64_t aligned_end = QEMU_ALIGN_DOWN(offset + bytes, cluster_size);
    int64_t aligned_bytes;

    if (aligned_end <= aligned_offset) {
        return 0;
    }
    aligned_bytes = aligned_end - aligned_offset;

    WITH_QEMU_LOCK_GUARD(&s->lock) {
        bdrv_reset_dirty_bitmap(s->access_bitmap, aligned_offset,
                                aligned_bytes);
    }

    block_copy_reset(s->bcs, aligned_offset, aligned_bytes);

    return bdrv_co_pdiscard(s->target, aligned_offset, aligned_bytes);
}

static void GRAPH_RDLOCK cbw_refresh_filename(BlockDriverState *bs)
{
    pstrcpy(bs->exact_filename, sizeof(bs->exact_filename),
            bs->file->bs->filename);
}

static void GRAPH_RDLOCK
cbw_child_perm(BlockDriverState *bs, BdrvChild *c, BdrvChildRole role,
               BlockReopenQueue *reopen_queue,
               uint64_t perm, uint64_t shared,
               uint64_t *nperm, uint64_t *nshared)
{
    BDRVCopyBeforeWriteState *s = bs->opaque;

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
            /*
             * Note, that source child may be shared with backup job. Backup job
             * does create own blk parent on copy-before-write node, so this
             * works even if source node does not have any parents before backup
             * start
             */
            *nperm = *nperm | BLK_PERM_CONSISTENT_READ;
            if (s->discard_source) {
                *nperm = *nperm | BLK_PERM_WRITE;
            }

            *nshared &= ~(BLK_PERM_WRITE | BLK_PERM_RESIZE);
        }
    }
}

static BlockdevOptions *cbw_parse_options(QDict *options, Error **errp)
{
    BlockdevOptions *opts = NULL;
    Visitor *v = NULL;

    qdict_put_str(options, "driver", "copy-before-write");

    v = qobject_input_visitor_new_flat_confused(options, errp);
    if (!v) {
        goto out;
    }

    visit_type_BlockdevOptions(v, NULL, &opts, errp);
    if (!opts) {
        goto out;
    }

    /*
     * Delete options which we are going to parse through BlockdevOptions
     * object for original options.
     */
    qdict_extract_subqdict(options, NULL, "bitmap");
    qdict_del(options, "on-cbw-error");
    qdict_del(options, "cbw-timeout");
    qdict_del(options, "min-cluster-size");

out:
    visit_free(v);
    qdict_del(options, "driver");

    return opts;
}

static int cbw_open(BlockDriverState *bs, QDict *options, int flags,
                    Error **errp)
{
    ERRP_GUARD();
    BDRVCopyBeforeWriteState *s = bs->opaque;
    BdrvDirtyBitmap *bitmap = NULL;
    int64_t cluster_size;
    g_autoptr(BlockdevOptions) full_opts = NULL;
    BlockdevOptionsCbw *opts;
    int ret;

    full_opts = cbw_parse_options(options, errp);
    if (!full_opts) {
        return -EINVAL;
    }
    assert(full_opts->driver == BLOCKDEV_DRIVER_COPY_BEFORE_WRITE);
    opts = &full_opts->u.copy_before_write;

    ret = bdrv_open_file_child(NULL, options, "file", bs, errp);
    if (ret < 0) {
        return ret;
    }

    s->target = bdrv_open_child(NULL, options, "target", bs, &child_of_bds,
                                BDRV_CHILD_DATA, false, errp);
    if (!s->target) {
        return -EINVAL;
    }

    GRAPH_RDLOCK_GUARD_MAINLOOP();

    if (opts->bitmap) {
        bitmap = block_dirty_bitmap_lookup(opts->bitmap->node,
                                           opts->bitmap->name, NULL, errp);
        if (!bitmap) {
            return -EINVAL;
        }
    }
    s->on_cbw_error = opts->has_on_cbw_error ? opts->on_cbw_error :
            ON_CBW_ERROR_BREAK_GUEST_WRITE;
    s->cbw_timeout_ns = opts->has_cbw_timeout ?
        opts->cbw_timeout * NANOSECONDS_PER_SECOND : 0;

    bs->total_sectors = bs->file->bs->total_sectors;
    bs->supported_write_flags = BDRV_REQ_WRITE_UNCHANGED |
            (BDRV_REQ_FUA & bs->file->bs->supported_write_flags);
    bs->supported_zero_flags = BDRV_REQ_WRITE_UNCHANGED |
            ((BDRV_REQ_FUA | BDRV_REQ_MAY_UNMAP | BDRV_REQ_NO_FALLBACK) &
             bs->file->bs->supported_zero_flags);

    s->discard_source = flags & BDRV_O_CBW_DISCARD_SOURCE;

    s->bcs = block_copy_state_new(bs->file, s->target, bs, bitmap,
                                  flags & BDRV_O_CBW_DISCARD_SOURCE,
                                  opts->min_cluster_size, errp);
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

static BlockDriver bdrv_cbw_filter = {
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
                                  bool discard_source,
                                  uint64_t min_cluster_size,
                                  BlockCopyState **bcs,
                                  Error **errp)
{
    BDRVCopyBeforeWriteState *state;
    BlockDriverState *top;
    QDict *opts;
    int flags = BDRV_O_RDWR | (discard_source ? BDRV_O_CBW_DISCARD_SOURCE : 0);

    assert(source->total_sectors == target->total_sectors);
    GLOBAL_STATE_CODE();

    opts = qdict_new();
    qdict_put_str(opts, "driver", "copy-before-write");
    if (filter_node_name) {
        qdict_put_str(opts, "node-name", filter_node_name);
    }
    qdict_put_str(opts, "file", bdrv_get_node_name(source));
    qdict_put_str(opts, "target", bdrv_get_node_name(target));

    if (min_cluster_size > INT64_MAX) {
        error_setg(errp, "min-cluster-size too large: %" PRIu64 " > %" PRIi64,
                   min_cluster_size, INT64_MAX);
        qobject_unref(opts);
        return NULL;
    }
    qdict_put_int(opts, "min-cluster-size", (int64_t)min_cluster_size);

    top = bdrv_insert_node(source, opts, flags, errp);
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
