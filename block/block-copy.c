/*
 * block_copy API
 *
 * Copyright (C) 2013 Proxmox Server Solutions
 * Copyright (c) 2019 Virtuozzo International GmbH.
 *
 * Authors:
 *  Dietmar Maurer (dietmar@proxmox.com)
 *  Vladimir Sementsov-Ogievskiy <vsementsov@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "trace.h"
#include "qapi/error.h"
#include "block/block-copy.h"
#include "sysemu/block-backend.h"
#include "qemu/units.h"

#define BLOCK_COPY_MAX_COPY_RANGE (16 * MiB)
#define BLOCK_COPY_MAX_BUFFER (1 * MiB)
#define BLOCK_COPY_MAX_MEM (128 * MiB)

typedef struct BlockCopyInFlightReq {
    int64_t offset;
    int64_t bytes;
    QLIST_ENTRY(BlockCopyInFlightReq) list;
    CoQueue wait_queue; /* coroutines blocked on this request */
} BlockCopyInFlightReq;

typedef struct BlockCopyState {
    /*
     * BdrvChild objects are not owned or managed by block-copy. They are
     * provided by block-copy user and user is responsible for appropriate
     * permissions on these children.
     */
    BdrvChild *source;
    BdrvChild *target;
    BdrvDirtyBitmap *copy_bitmap;
    int64_t in_flight_bytes;
    int64_t cluster_size;
    bool use_copy_range;
    int64_t copy_size;
    uint64_t len;
    QLIST_HEAD(, BlockCopyInFlightReq) inflight_reqs;

    BdrvRequestFlags write_flags;

    /*
     * skip_unallocated:
     *
     * Used by sync=top jobs, which first scan the source node for unallocated
     * areas and clear them in the copy_bitmap.  During this process, the bitmap
     * is thus not fully initialized: It may still have bits set for areas that
     * are unallocated and should actually not be copied.
     *
     * This is indicated by skip_unallocated.
     *
     * In this case, block_copy() will query the source’s allocation status,
     * skip unallocated regions, clear them in the copy_bitmap, and invoke
     * block_copy_reset_unallocated() every time it does.
     */
    bool skip_unallocated;

    ProgressMeter *progress;
    /* progress_bytes_callback: called when some copying progress is done. */
    ProgressBytesCallbackFunc progress_bytes_callback;
    void *progress_opaque;

    SharedResource *mem;
} BlockCopyState;

static BlockCopyInFlightReq *find_conflicting_inflight_req(BlockCopyState *s,
                                                           int64_t offset,
                                                           int64_t bytes)
{
    BlockCopyInFlightReq *req;

    QLIST_FOREACH(req, &s->inflight_reqs, list) {
        if (offset + bytes > req->offset && offset < req->offset + req->bytes) {
            return req;
        }
    }

    return NULL;
}

/*
 * If there are no intersecting requests return false. Otherwise, wait for the
 * first found intersecting request to finish and return true.
 */
static bool coroutine_fn block_copy_wait_one(BlockCopyState *s, int64_t offset,
                                             int64_t bytes)
{
    BlockCopyInFlightReq *req = find_conflicting_inflight_req(s, offset, bytes);

    if (!req) {
        return false;
    }

    qemu_co_queue_wait(&req->wait_queue, NULL);

    return true;
}

/* Called only on full-dirty region */
static void block_copy_inflight_req_begin(BlockCopyState *s,
                                          BlockCopyInFlightReq *req,
                                          int64_t offset, int64_t bytes)
{
    assert(!find_conflicting_inflight_req(s, offset, bytes));

    bdrv_reset_dirty_bitmap(s->copy_bitmap, offset, bytes);
    s->in_flight_bytes += bytes;

    req->offset = offset;
    req->bytes = bytes;
    qemu_co_queue_init(&req->wait_queue);
    QLIST_INSERT_HEAD(&s->inflight_reqs, req, list);
}

/*
 * block_copy_inflight_req_shrink
 *
 * Drop the tail of the request to be handled later. Set dirty bits back and
 * wake up all requests waiting for us (may be some of them are not intersecting
 * with shrunk request)
 */
static void coroutine_fn block_copy_inflight_req_shrink(BlockCopyState *s,
        BlockCopyInFlightReq *req, int64_t new_bytes)
{
    if (new_bytes == req->bytes) {
        return;
    }

    assert(new_bytes > 0 && new_bytes < req->bytes);

    s->in_flight_bytes -= req->bytes - new_bytes;
    bdrv_set_dirty_bitmap(s->copy_bitmap,
                          req->offset + new_bytes, req->bytes - new_bytes);

    req->bytes = new_bytes;
    qemu_co_queue_restart_all(&req->wait_queue);
}

static void coroutine_fn block_copy_inflight_req_end(BlockCopyState *s,
                                                     BlockCopyInFlightReq *req,
                                                     int ret)
{
    s->in_flight_bytes -= req->bytes;
    if (ret < 0) {
        bdrv_set_dirty_bitmap(s->copy_bitmap, req->offset, req->bytes);
    }
    QLIST_REMOVE(req, list);
    qemu_co_queue_restart_all(&req->wait_queue);
}

void block_copy_state_free(BlockCopyState *s)
{
    if (!s) {
        return;
    }

    bdrv_release_dirty_bitmap(s->copy_bitmap);
    shres_destroy(s->mem);
    g_free(s);
}

static uint32_t block_copy_max_transfer(BdrvChild *source, BdrvChild *target)
{
    return MIN_NON_ZERO(INT_MAX,
                        MIN_NON_ZERO(source->bs->bl.max_transfer,
                                     target->bs->bl.max_transfer));
}

BlockCopyState *block_copy_state_new(BdrvChild *source, BdrvChild *target,
                                     int64_t cluster_size,
                                     BdrvRequestFlags write_flags, Error **errp)
{
    BlockCopyState *s;
    BdrvDirtyBitmap *copy_bitmap;

    copy_bitmap = bdrv_create_dirty_bitmap(source->bs, cluster_size, NULL,
                                           errp);
    if (!copy_bitmap) {
        return NULL;
    }
    bdrv_disable_dirty_bitmap(copy_bitmap);

    s = g_new(BlockCopyState, 1);
    *s = (BlockCopyState) {
        .source = source,
        .target = target,
        .copy_bitmap = copy_bitmap,
        .cluster_size = cluster_size,
        .len = bdrv_dirty_bitmap_size(copy_bitmap),
        .write_flags = write_flags,
        .mem = shres_create(BLOCK_COPY_MAX_MEM),
    };

    if (block_copy_max_transfer(source, target) < cluster_size) {
        /*
         * copy_range does not respect max_transfer. We don't want to bother
         * with requests smaller than block-copy cluster size, so fallback to
         * buffered copying (read and write respect max_transfer on their
         * behalf).
         */
        s->use_copy_range = false;
        s->copy_size = cluster_size;
    } else if (write_flags & BDRV_REQ_WRITE_COMPRESSED) {
        /* Compression supports only cluster-size writes and no copy-range. */
        s->use_copy_range = false;
        s->copy_size = cluster_size;
    } else {
        /*
         * We enable copy-range, but keep small copy_size, until first
         * successful copy_range (look at block_copy_do_copy).
         */
        s->use_copy_range = true;
        s->copy_size = MAX(s->cluster_size, BLOCK_COPY_MAX_BUFFER);
    }

    QLIST_INIT(&s->inflight_reqs);

    return s;
}

void block_copy_set_progress_callback(
        BlockCopyState *s,
        ProgressBytesCallbackFunc progress_bytes_callback,
        void *progress_opaque)
{
    s->progress_bytes_callback = progress_bytes_callback;
    s->progress_opaque = progress_opaque;
}

void block_copy_set_progress_meter(BlockCopyState *s, ProgressMeter *pm)
{
    s->progress = pm;
}

/*
 * block_copy_do_copy
 *
 * Do copy of cluster-aligned chunk. Requested region is allowed to exceed
 * s->len only to cover last cluster when s->len is not aligned to clusters.
 *
 * No sync here: nor bitmap neighter intersecting requests handling, only copy.
 *
 * Returns 0 on success.
 */
static int coroutine_fn block_copy_do_copy(BlockCopyState *s,
                                           int64_t offset, int64_t bytes,
                                           bool zeroes, bool *error_is_read)
{
    int ret;
    int64_t nbytes = MIN(offset + bytes, s->len) - offset;
    void *bounce_buffer = NULL;

    assert(offset >= 0 && bytes > 0 && INT64_MAX - offset >= bytes);
    assert(QEMU_IS_ALIGNED(offset, s->cluster_size));
    assert(QEMU_IS_ALIGNED(bytes, s->cluster_size));
    assert(offset < s->len);
    assert(offset + bytes <= s->len ||
           offset + bytes == QEMU_ALIGN_UP(s->len, s->cluster_size));
    assert(nbytes < INT_MAX);

    if (zeroes) {
        ret = bdrv_co_pwrite_zeroes(s->target, offset, nbytes, s->write_flags &
                                    ~BDRV_REQ_WRITE_COMPRESSED);
        if (ret < 0) {
            trace_block_copy_write_zeroes_fail(s, offset, ret);
            if (error_is_read) {
                *error_is_read = false;
            }
        }
        return ret;
    }

    if (s->use_copy_range) {
        ret = bdrv_co_copy_range(s->source, offset, s->target, offset, nbytes,
                                 0, s->write_flags);
        if (ret < 0) {
            trace_block_copy_copy_range_fail(s, offset, ret);
            s->use_copy_range = false;
            s->copy_size = MAX(s->cluster_size, BLOCK_COPY_MAX_BUFFER);
            /* Fallback to read+write with allocated buffer */
        } else {
            if (s->use_copy_range) {
                /*
                 * Successful copy-range. Now increase copy_size.  copy_range
                 * does not respect max_transfer (it's a TODO), so we factor
                 * that in here.
                 *
                 * Note: we double-check s->use_copy_range for the case when
                 * parallel block-copy request unsets it during previous
                 * bdrv_co_copy_range call.
                 */
                s->copy_size =
                        MIN(MAX(s->cluster_size, BLOCK_COPY_MAX_COPY_RANGE),
                            QEMU_ALIGN_DOWN(block_copy_max_transfer(s->source,
                                                                    s->target),
                                            s->cluster_size));
            }
            goto out;
        }
    }

    /*
     * In case of failed copy_range request above, we may proceed with buffered
     * request larger than BLOCK_COPY_MAX_BUFFER. Still, further requests will
     * be properly limited, so don't care too much. Moreover the most likely
     * case (copy_range is unsupported for the configuration, so the very first
     * copy_range request fails) is handled by setting large copy_size only
     * after first successful copy_range.
     */

    bounce_buffer = qemu_blockalign(s->source->bs, nbytes);

    ret = bdrv_co_pread(s->source, offset, nbytes, bounce_buffer, 0);
    if (ret < 0) {
        trace_block_copy_read_fail(s, offset, ret);
        if (error_is_read) {
            *error_is_read = true;
        }
        goto out;
    }

    ret = bdrv_co_pwrite(s->target, offset, nbytes, bounce_buffer,
                         s->write_flags);
    if (ret < 0) {
        trace_block_copy_write_fail(s, offset, ret);
        if (error_is_read) {
            *error_is_read = false;
        }
        goto out;
    }

out:
    qemu_vfree(bounce_buffer);

    return ret;
}

static int block_copy_block_status(BlockCopyState *s, int64_t offset,
                                   int64_t bytes, int64_t *pnum)
{
    int64_t num;
    BlockDriverState *base;
    int ret;

    if (s->skip_unallocated && s->source->bs->backing) {
        base = s->source->bs->backing->bs;
    } else {
        base = NULL;
    }

    ret = bdrv_block_status_above(s->source->bs, base, offset, bytes, &num,
                                  NULL, NULL);
    if (ret < 0 || num < s->cluster_size) {
        /*
         * On error or if failed to obtain large enough chunk just fallback to
         * copy one cluster.
         */
        num = s->cluster_size;
        ret = BDRV_BLOCK_ALLOCATED | BDRV_BLOCK_DATA;
    } else if (offset + num == s->len) {
        num = QEMU_ALIGN_UP(num, s->cluster_size);
    } else {
        num = QEMU_ALIGN_DOWN(num, s->cluster_size);
    }

    *pnum = num;
    return ret;
}

/*
 * Check if the cluster starting at offset is allocated or not.
 * return via pnum the number of contiguous clusters sharing this allocation.
 */
static int block_copy_is_cluster_allocated(BlockCopyState *s, int64_t offset,
                                           int64_t *pnum)
{
    BlockDriverState *bs = s->source->bs;
    int64_t count, total_count = 0;
    int64_t bytes = s->len - offset;
    int ret;

    assert(QEMU_IS_ALIGNED(offset, s->cluster_size));

    while (true) {
        ret = bdrv_is_allocated(bs, offset, bytes, &count);
        if (ret < 0) {
            return ret;
        }

        total_count += count;

        if (ret || count == 0) {
            /*
             * ret: partial segment(s) are considered allocated.
             * otherwise: unallocated tail is treated as an entire segment.
             */
            *pnum = DIV_ROUND_UP(total_count, s->cluster_size);
            return ret;
        }

        /* Unallocated segment(s) with uncertain following segment(s) */
        if (total_count >= s->cluster_size) {
            *pnum = total_count / s->cluster_size;
            return 0;
        }

        offset += count;
        bytes -= count;
    }
}

/*
 * Reset bits in copy_bitmap starting at offset if they represent unallocated
 * data in the image. May reset subsequent contiguous bits.
 * @return 0 when the cluster at @offset was unallocated,
 *         1 otherwise, and -ret on error.
 */
int64_t block_copy_reset_unallocated(BlockCopyState *s,
                                     int64_t offset, int64_t *count)
{
    int ret;
    int64_t clusters, bytes;

    ret = block_copy_is_cluster_allocated(s, offset, &clusters);
    if (ret < 0) {
        return ret;
    }

    bytes = clusters * s->cluster_size;

    if (!ret) {
        bdrv_reset_dirty_bitmap(s->copy_bitmap, offset, bytes);
        progress_set_remaining(s->progress,
                               bdrv_get_dirty_count(s->copy_bitmap) +
                               s->in_flight_bytes);
    }

    *count = bytes;
    return ret;
}

/*
 * block_copy_dirty_clusters
 *
 * Copy dirty clusters in @offset/@bytes range.
 * Returns 1 if dirty clusters found and successfully copied, 0 if no dirty
 * clusters found and -errno on failure.
 */
static int coroutine_fn block_copy_dirty_clusters(BlockCopyState *s,
                                                  int64_t offset, int64_t bytes,
                                                  bool *error_is_read)
{
    int ret = 0;
    bool found_dirty = false;

    /*
     * block_copy() user is responsible for keeping source and target in same
     * aio context
     */
    assert(bdrv_get_aio_context(s->source->bs) ==
           bdrv_get_aio_context(s->target->bs));

    assert(QEMU_IS_ALIGNED(offset, s->cluster_size));
    assert(QEMU_IS_ALIGNED(bytes, s->cluster_size));

    while (bytes) {
        BlockCopyInFlightReq req;
        int64_t next_zero, cur_bytes, status_bytes;

        if (!bdrv_dirty_bitmap_get(s->copy_bitmap, offset)) {
            trace_block_copy_skip(s, offset);
            offset += s->cluster_size;
            bytes -= s->cluster_size;
            continue; /* already copied */
        }

        found_dirty = true;

        cur_bytes = MIN(bytes, s->copy_size);

        next_zero = bdrv_dirty_bitmap_next_zero(s->copy_bitmap, offset,
                                                cur_bytes);
        if (next_zero >= 0) {
            assert(next_zero > offset); /* offset is dirty */
            assert(next_zero < offset + cur_bytes); /* no need to do MIN() */
            cur_bytes = next_zero - offset;
        }
        block_copy_inflight_req_begin(s, &req, offset, cur_bytes);

        ret = block_copy_block_status(s, offset, cur_bytes, &status_bytes);
        assert(ret >= 0); /* never fail */
        cur_bytes = MIN(cur_bytes, status_bytes);
        block_copy_inflight_req_shrink(s, &req, cur_bytes);
        if (s->skip_unallocated && !(ret & BDRV_BLOCK_ALLOCATED)) {
            block_copy_inflight_req_end(s, &req, 0);
            progress_set_remaining(s->progress,
                                   bdrv_get_dirty_count(s->copy_bitmap) +
                                   s->in_flight_bytes);
            trace_block_copy_skip_range(s, offset, status_bytes);
            offset += status_bytes;
            bytes -= status_bytes;
            continue;
        }

        trace_block_copy_process(s, offset);

        co_get_from_shres(s->mem, cur_bytes);
        ret = block_copy_do_copy(s, offset, cur_bytes, ret & BDRV_BLOCK_ZERO,
                                 error_is_read);
        co_put_to_shres(s->mem, cur_bytes);
        block_copy_inflight_req_end(s, &req, ret);
        if (ret < 0) {
            return ret;
        }

        progress_work_done(s->progress, cur_bytes);
        s->progress_bytes_callback(cur_bytes, s->progress_opaque);
        offset += cur_bytes;
        bytes -= cur_bytes;
    }

    return found_dirty;
}

/*
 * block_copy
 *
 * Copy requested region, accordingly to dirty bitmap.
 * Collaborate with parallel block_copy requests: if they succeed it will help
 * us. If they fail, we will retry not-copied regions. So, if we return error,
 * it means that some I/O operation failed in context of _this_ block_copy call,
 * not some parallel operation.
 */
int coroutine_fn block_copy(BlockCopyState *s, int64_t offset, int64_t bytes,
                            bool *error_is_read)
{
    int ret;

    do {
        ret = block_copy_dirty_clusters(s, offset, bytes, error_is_read);

        if (ret == 0) {
            ret = block_copy_wait_one(s, offset, bytes);
        }

        /*
         * We retry in two cases:
         * 1. Some progress done
         *    Something was copied, which means that there were yield points
         *    and some new dirty bits may have appeared (due to failed parallel
         *    block-copy requests).
         * 2. We have waited for some intersecting block-copy request
         *    It may have failed and produced new dirty bits.
         */
    } while (ret > 0);

    return ret;
}

BdrvDirtyBitmap *block_copy_dirty_bitmap(BlockCopyState *s)
{
    return s->copy_bitmap;
}

void block_copy_set_skip_unallocated(BlockCopyState *s, bool skip)
{
    s->skip_unallocated = skip;
}
