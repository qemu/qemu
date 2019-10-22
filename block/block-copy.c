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

static void coroutine_fn block_copy_wait_inflight_reqs(BlockCopyState *s,
                                                       int64_t start,
                                                       int64_t end)
{
    BlockCopyInFlightReq *req;
    bool waited;

    do {
        waited = false;
        QLIST_FOREACH(req, &s->inflight_reqs, list) {
            if (end > req->start_byte && start < req->end_byte) {
                qemu_co_queue_wait(&req->wait_queue, NULL);
                waited = true;
                break;
            }
        }
    } while (waited);
}

static void block_copy_inflight_req_begin(BlockCopyState *s,
                                          BlockCopyInFlightReq *req,
                                          int64_t start, int64_t end)
{
    req->start_byte = start;
    req->end_byte = end;
    qemu_co_queue_init(&req->wait_queue);
    QLIST_INSERT_HEAD(&s->inflight_reqs, req, list);
}

static void coroutine_fn block_copy_inflight_req_end(BlockCopyInFlightReq *req)
{
    QLIST_REMOVE(req, list);
    qemu_co_queue_restart_all(&req->wait_queue);
}

void block_copy_state_free(BlockCopyState *s)
{
    if (!s) {
        return;
    }

    bdrv_release_dirty_bitmap(s->copy_bitmap);
    g_free(s);
}

BlockCopyState *block_copy_state_new(BdrvChild *source, BdrvChild *target,
                                     int64_t cluster_size,
                                     BdrvRequestFlags write_flags, Error **errp)
{
    BlockCopyState *s;
    BdrvDirtyBitmap *copy_bitmap;

    /* Ignore BLOCK_COPY_MAX_COPY_RANGE if requested cluster_size is larger */
    uint32_t max_transfer =
            MIN_NON_ZERO(MAX(cluster_size, BLOCK_COPY_MAX_COPY_RANGE),
                         MIN_NON_ZERO(source->bs->bl.max_transfer,
                                      target->bs->bl.max_transfer));

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
    };

    s->copy_range_size = QEMU_ALIGN_DOWN(max_transfer, cluster_size),
    /*
     * Set use_copy_range, consider the following:
     * 1. Compression is not supported for copy_range.
     * 2. copy_range does not respect max_transfer (it's a TODO), so we factor
     *    that in here. If max_transfer is smaller than the job->cluster_size,
     *    we do not use copy_range (in that case it's zero after aligning down
     *    above).
     */
    s->use_copy_range =
        !(write_flags & BDRV_REQ_WRITE_COMPRESSED) && s->copy_range_size > 0;

    QLIST_INIT(&s->inflight_reqs);

    return s;
}

void block_copy_set_callbacks(
        BlockCopyState *s,
        ProgressBytesCallbackFunc progress_bytes_callback,
        ProgressResetCallbackFunc progress_reset_callback,
        void *progress_opaque)
{
    s->progress_bytes_callback = progress_bytes_callback;
    s->progress_reset_callback = progress_reset_callback;
    s->progress_opaque = progress_opaque;
}

/*
 * Copy range to target with a bounce buffer and return the bytes copied. If
 * error occurred, return a negative error number
 */
static int coroutine_fn block_copy_with_bounce_buffer(BlockCopyState *s,
                                                      int64_t start,
                                                      int64_t end,
                                                      bool *error_is_read)
{
    int ret;
    int nbytes;
    void *bounce_buffer = qemu_blockalign(s->source->bs, s->cluster_size);

    assert(QEMU_IS_ALIGNED(start, s->cluster_size));
    bdrv_reset_dirty_bitmap(s->copy_bitmap, start, s->cluster_size);
    nbytes = MIN(s->cluster_size, s->len - start);

    ret = bdrv_co_pread(s->source, start, nbytes, bounce_buffer, 0);
    if (ret < 0) {
        trace_block_copy_with_bounce_buffer_read_fail(s, start, ret);
        if (error_is_read) {
            *error_is_read = true;
        }
        goto fail;
    }

    ret = bdrv_co_pwrite(s->target, start, nbytes, bounce_buffer,
                         s->write_flags);
    if (ret < 0) {
        trace_block_copy_with_bounce_buffer_write_fail(s, start, ret);
        if (error_is_read) {
            *error_is_read = false;
        }
        goto fail;
    }

    qemu_vfree(bounce_buffer);

    return nbytes;
fail:
    qemu_vfree(bounce_buffer);
    bdrv_set_dirty_bitmap(s->copy_bitmap, start, s->cluster_size);
    return ret;

}

/*
 * Copy range to target and return the bytes copied. If error occurred, return a
 * negative error number.
 */
static int coroutine_fn block_copy_with_offload(BlockCopyState *s,
                                                int64_t start,
                                                int64_t end)
{
    int ret;
    int nr_clusters;
    int nbytes;

    assert(QEMU_IS_ALIGNED(s->copy_range_size, s->cluster_size));
    assert(QEMU_IS_ALIGNED(start, s->cluster_size));
    nbytes = MIN(s->copy_range_size, MIN(end, s->len) - start);
    nr_clusters = DIV_ROUND_UP(nbytes, s->cluster_size);
    bdrv_reset_dirty_bitmap(s->copy_bitmap, start,
                            s->cluster_size * nr_clusters);
    ret = bdrv_co_copy_range(s->source, start, s->target, start, nbytes,
                             0, s->write_flags);
    if (ret < 0) {
        trace_block_copy_with_offload_fail(s, start, ret);
        bdrv_set_dirty_bitmap(s->copy_bitmap, start,
                              s->cluster_size * nr_clusters);
        return ret;
    }

    return nbytes;
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
        s->progress_reset_callback(s->progress_opaque);
    }

    *count = bytes;
    return ret;
}

int coroutine_fn block_copy(BlockCopyState *s,
                            int64_t start, uint64_t bytes,
                            bool *error_is_read)
{
    int ret = 0;
    int64_t end = bytes + start; /* bytes */
    int64_t status_bytes;
    BlockCopyInFlightReq req;

    /*
     * block_copy() user is responsible for keeping source and target in same
     * aio context
     */
    assert(bdrv_get_aio_context(s->source->bs) ==
           bdrv_get_aio_context(s->target->bs));

    assert(QEMU_IS_ALIGNED(start, s->cluster_size));
    assert(QEMU_IS_ALIGNED(end, s->cluster_size));

    block_copy_wait_inflight_reqs(s, start, bytes);
    block_copy_inflight_req_begin(s, &req, start, end);

    while (start < end) {
        int64_t dirty_end;

        if (!bdrv_dirty_bitmap_get(s->copy_bitmap, start)) {
            trace_block_copy_skip(s, start);
            start += s->cluster_size;
            continue; /* already copied */
        }

        dirty_end = bdrv_dirty_bitmap_next_zero(s->copy_bitmap, start,
                                                (end - start));
        if (dirty_end < 0) {
            dirty_end = end;
        }

        if (s->skip_unallocated) {
            ret = block_copy_reset_unallocated(s, start, &status_bytes);
            if (ret == 0) {
                trace_block_copy_skip_range(s, start, status_bytes);
                start += status_bytes;
                continue;
            }
            /* Clamp to known allocated region */
            dirty_end = MIN(dirty_end, start + status_bytes);
        }

        trace_block_copy_process(s, start);

        if (s->use_copy_range) {
            ret = block_copy_with_offload(s, start, dirty_end);
            if (ret < 0) {
                s->use_copy_range = false;
            }
        }
        if (!s->use_copy_range) {
            ret = block_copy_with_bounce_buffer(s, start, dirty_end,
                                                error_is_read);
        }
        if (ret < 0) {
            break;
        }

        start += ret;
        s->progress_bytes_callback(ret, s->progress_opaque);
        ret = 0;
    }

    block_copy_inflight_req_end(&req);

    return ret;
}
