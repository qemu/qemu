/*
 * QEMU backup
 *
 * Copyright (C) 2013 Proxmox Server Solutions
 *
 * Authors:
 *  Dietmar Maurer (dietmar@proxmox.com)
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "trace.h"
#include "block/block.h"
#include "block/block_int.h"
#include "block/blockjob_int.h"
#include "block/block_backup.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/ratelimit.h"
#include "qemu/cutils.h"
#include "sysemu/block-backend.h"
#include "qemu/bitmap.h"
#include "qemu/error-report.h"

#define BACKUP_CLUSTER_SIZE_DEFAULT (1 << 16)

typedef struct CowRequest {
    int64_t start_byte;
    int64_t end_byte;
    QLIST_ENTRY(CowRequest) list;
    CoQueue wait_queue; /* coroutines blocked on this request */
} CowRequest;

typedef void (*ProgressBytesCallbackFunc)(int64_t bytes, void *opaque);
typedef void (*ProgressResetCallbackFunc)(void *opaque);
typedef struct BlockCopyState {
    BlockBackend *source;
    BlockBackend *target;
    BdrvDirtyBitmap *copy_bitmap;
    int64_t cluster_size;
    bool use_copy_range;
    int64_t copy_range_size;
    uint64_t len;

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

    /* progress_bytes_callback: called when some copying progress is done. */
    ProgressBytesCallbackFunc progress_bytes_callback;

    /*
     * progress_reset_callback: called when some bytes reset from copy_bitmap
     * (see @skip_unallocated above). The callee is assumed to recalculate how
     * many bytes remain based on the dirty bit count of copy_bitmap.
     */
    ProgressResetCallbackFunc progress_reset_callback;
    void *progress_opaque;
} BlockCopyState;

typedef struct BackupBlockJob {
    BlockJob common;
    BlockDriverState *source_bs;

    BdrvDirtyBitmap *sync_bitmap;

    MirrorSyncMode sync_mode;
    BitmapSyncMode bitmap_mode;
    BlockdevOnError on_source_error;
    BlockdevOnError on_target_error;
    CoRwlock flush_rwlock;
    uint64_t len;
    uint64_t bytes_read;
    int64_t cluster_size;
    NotifierWithReturn before_write;
    QLIST_HEAD(, CowRequest) inflight_reqs;

    BlockCopyState *bcs;
} BackupBlockJob;

static const BlockJobDriver backup_job_driver;

/* See if in-flight requests overlap and wait for them to complete */
static void coroutine_fn wait_for_overlapping_requests(BackupBlockJob *job,
                                                       int64_t start,
                                                       int64_t end)
{
    CowRequest *req;
    bool retry;

    do {
        retry = false;
        QLIST_FOREACH(req, &job->inflight_reqs, list) {
            if (end > req->start_byte && start < req->end_byte) {
                qemu_co_queue_wait(&req->wait_queue, NULL);
                retry = true;
                break;
            }
        }
    } while (retry);
}

/* Keep track of an in-flight request */
static void cow_request_begin(CowRequest *req, BackupBlockJob *job,
                              int64_t start, int64_t end)
{
    req->start_byte = start;
    req->end_byte = end;
    qemu_co_queue_init(&req->wait_queue);
    QLIST_INSERT_HEAD(&job->inflight_reqs, req, list);
}

/* Forget about a completed request */
static void cow_request_end(CowRequest *req)
{
    QLIST_REMOVE(req, list);
    qemu_co_queue_restart_all(&req->wait_queue);
}

static void block_copy_state_free(BlockCopyState *s)
{
    if (!s) {
        return;
    }

    bdrv_release_dirty_bitmap(blk_bs(s->source), s->copy_bitmap);
    blk_unref(s->source);
    blk_unref(s->target);
    g_free(s);
}

static BlockCopyState *block_copy_state_new(
        BlockDriverState *source, BlockDriverState *target,
        int64_t cluster_size, BdrvRequestFlags write_flags,
        ProgressBytesCallbackFunc progress_bytes_callback,
        ProgressResetCallbackFunc progress_reset_callback,
        void *progress_opaque, Error **errp)
{
    BlockCopyState *s;
    int ret;
    uint64_t no_resize = BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE |
                         BLK_PERM_WRITE_UNCHANGED | BLK_PERM_GRAPH_MOD;
    BdrvDirtyBitmap *copy_bitmap;

    copy_bitmap = bdrv_create_dirty_bitmap(source, cluster_size, NULL, errp);
    if (!copy_bitmap) {
        return NULL;
    }
    bdrv_disable_dirty_bitmap(copy_bitmap);

    s = g_new(BlockCopyState, 1);
    *s = (BlockCopyState) {
        .source = blk_new(bdrv_get_aio_context(source),
                          BLK_PERM_CONSISTENT_READ, no_resize),
        .target = blk_new(bdrv_get_aio_context(target),
                          BLK_PERM_WRITE, no_resize),
        .copy_bitmap = copy_bitmap,
        .cluster_size = cluster_size,
        .len = bdrv_dirty_bitmap_size(copy_bitmap),
        .write_flags = write_flags,
        .progress_bytes_callback = progress_bytes_callback,
        .progress_reset_callback = progress_reset_callback,
        .progress_opaque = progress_opaque,
    };

    s->copy_range_size = QEMU_ALIGN_DOWN(MIN(blk_get_max_transfer(s->source),
                                             blk_get_max_transfer(s->target)),
                                         s->cluster_size);
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

    /*
     * We just allow aio context change on our block backends. block_copy() user
     * (now it's only backup) is responsible for source and target being in same
     * aio context.
     */
    blk_set_disable_request_queuing(s->source, true);
    blk_set_allow_aio_context_change(s->source, true);
    blk_set_disable_request_queuing(s->target, true);
    blk_set_allow_aio_context_change(s->target, true);

    ret = blk_insert_bs(s->source, source, errp);
    if (ret < 0) {
        goto fail;
    }

    ret = blk_insert_bs(s->target, target, errp);
    if (ret < 0) {
        goto fail;
    }

    return s;

fail:
    block_copy_state_free(s);

    return NULL;
}

/* Copy range to target with a bounce buffer and return the bytes copied. If
 * error occurred, return a negative error number */
static int coroutine_fn block_copy_with_bounce_buffer(BlockCopyState *s,
                                                      int64_t start,
                                                      int64_t end,
                                                      bool is_write_notifier,
                                                      bool *error_is_read,
                                                      void **bounce_buffer)
{
    int ret;
    int nbytes;
    int read_flags = is_write_notifier ? BDRV_REQ_NO_SERIALISING : 0;

    assert(QEMU_IS_ALIGNED(start, s->cluster_size));
    bdrv_reset_dirty_bitmap(s->copy_bitmap, start, s->cluster_size);
    nbytes = MIN(s->cluster_size, s->len - start);
    if (!*bounce_buffer) {
        *bounce_buffer = blk_blockalign(s->source, s->cluster_size);
    }

    ret = blk_co_pread(s->source, start, nbytes, *bounce_buffer, read_flags);
    if (ret < 0) {
        trace_block_copy_with_bounce_buffer_read_fail(s, start, ret);
        if (error_is_read) {
            *error_is_read = true;
        }
        goto fail;
    }

    ret = blk_co_pwrite(s->target, start, nbytes, *bounce_buffer,
                        s->write_flags);
    if (ret < 0) {
        trace_block_copy_with_bounce_buffer_write_fail(s, start, ret);
        if (error_is_read) {
            *error_is_read = false;
        }
        goto fail;
    }

    return nbytes;
fail:
    bdrv_set_dirty_bitmap(s->copy_bitmap, start, s->cluster_size);
    return ret;

}

/* Copy range to target and return the bytes copied. If error occurred, return a
 * negative error number. */
static int coroutine_fn block_copy_with_offload(BlockCopyState *s,
                                                int64_t start,
                                                int64_t end,
                                                bool is_write_notifier)
{
    int ret;
    int nr_clusters;
    int nbytes;
    int read_flags = is_write_notifier ? BDRV_REQ_NO_SERIALISING : 0;

    assert(QEMU_IS_ALIGNED(s->copy_range_size, s->cluster_size));
    assert(QEMU_IS_ALIGNED(start, s->cluster_size));
    nbytes = MIN(s->copy_range_size, MIN(end, s->len) - start);
    nr_clusters = DIV_ROUND_UP(nbytes, s->cluster_size);
    bdrv_reset_dirty_bitmap(s->copy_bitmap, start,
                            s->cluster_size * nr_clusters);
    ret = blk_co_copy_range(s->source, start, s->target, start, nbytes,
                            read_flags, s->write_flags);
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
    BlockDriverState *bs = blk_bs(s->source);
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

/**
 * Reset bits in copy_bitmap starting at offset if they represent unallocated
 * data in the image. May reset subsequent contiguous bits.
 * @return 0 when the cluster at @offset was unallocated,
 *         1 otherwise, and -ret on error.
 */
static int64_t block_copy_reset_unallocated(BlockCopyState *s,
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

static int coroutine_fn block_copy(BlockCopyState *s,
                                   int64_t start, uint64_t bytes,
                                   bool *error_is_read,
                                   bool is_write_notifier)
{
    int ret = 0;
    int64_t end = bytes + start; /* bytes */
    void *bounce_buffer = NULL;
    int64_t status_bytes;

    /*
     * block_copy() user is responsible for keeping source and target in same
     * aio context
     */
    assert(blk_get_aio_context(s->source) == blk_get_aio_context(s->target));

    assert(QEMU_IS_ALIGNED(start, s->cluster_size));
    assert(QEMU_IS_ALIGNED(end, s->cluster_size));

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
            ret = block_copy_with_offload(s, start, dirty_end,
                                          is_write_notifier);
            if (ret < 0) {
                s->use_copy_range = false;
            }
        }
        if (!s->use_copy_range) {
            ret = block_copy_with_bounce_buffer(s, start, dirty_end,
                                                is_write_notifier,
                                                error_is_read, &bounce_buffer);
        }
        if (ret < 0) {
            break;
        }

        start += ret;
        s->progress_bytes_callback(ret, s->progress_opaque);
        ret = 0;
    }

    if (bounce_buffer) {
        qemu_vfree(bounce_buffer);
    }

    return ret;
}

static void backup_progress_bytes_callback(int64_t bytes, void *opaque)
{
    BackupBlockJob *s = opaque;

    s->bytes_read += bytes;
    job_progress_update(&s->common.job, bytes);
}

static void backup_progress_reset_callback(void *opaque)
{
    BackupBlockJob *s = opaque;
    uint64_t estimate = bdrv_get_dirty_count(s->bcs->copy_bitmap);

    job_progress_set_remaining(&s->common.job, estimate);
}

static int coroutine_fn backup_do_cow(BackupBlockJob *job,
                                      int64_t offset, uint64_t bytes,
                                      bool *error_is_read,
                                      bool is_write_notifier)
{
    CowRequest cow_request;
    int ret = 0;
    int64_t start, end; /* bytes */

    qemu_co_rwlock_rdlock(&job->flush_rwlock);

    start = QEMU_ALIGN_DOWN(offset, job->cluster_size);
    end = QEMU_ALIGN_UP(bytes + offset, job->cluster_size);

    trace_backup_do_cow_enter(job, start, offset, bytes);

    wait_for_overlapping_requests(job, start, end);
    cow_request_begin(&cow_request, job, start, end);

    ret = block_copy(job->bcs, start, end - start, error_is_read,
                     is_write_notifier);

    cow_request_end(&cow_request);

    trace_backup_do_cow_return(job, offset, bytes, ret);

    qemu_co_rwlock_unlock(&job->flush_rwlock);

    return ret;
}

static int coroutine_fn backup_before_write_notify(
        NotifierWithReturn *notifier,
        void *opaque)
{
    BackupBlockJob *job = container_of(notifier, BackupBlockJob, before_write);
    BdrvTrackedRequest *req = opaque;

    assert(req->bs == job->source_bs);
    assert(QEMU_IS_ALIGNED(req->offset, BDRV_SECTOR_SIZE));
    assert(QEMU_IS_ALIGNED(req->bytes, BDRV_SECTOR_SIZE));

    return backup_do_cow(job, req->offset, req->bytes, NULL, true);
}

static void backup_cleanup_sync_bitmap(BackupBlockJob *job, int ret)
{
    BdrvDirtyBitmap *bm;
    bool sync = (((ret == 0) || (job->bitmap_mode == BITMAP_SYNC_MODE_ALWAYS)) \
                 && (job->bitmap_mode != BITMAP_SYNC_MODE_NEVER));

    if (sync) {
        /*
         * We succeeded, or we always intended to sync the bitmap.
         * Delete this bitmap and install the child.
         */
        bm = bdrv_dirty_bitmap_abdicate(job->source_bs, job->sync_bitmap, NULL);
    } else {
        /*
         * We failed, or we never intended to sync the bitmap anyway.
         * Merge the successor back into the parent, keeping all data.
         */
        bm = bdrv_reclaim_dirty_bitmap(job->source_bs, job->sync_bitmap, NULL);
    }

    assert(bm);

    if (ret < 0 && job->bitmap_mode == BITMAP_SYNC_MODE_ALWAYS) {
        /* If we failed and synced, merge in the bits we didn't copy: */
        bdrv_dirty_bitmap_merge_internal(bm, job->bcs->copy_bitmap,
                                         NULL, true);
    }
}

static void backup_commit(Job *job)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common.job);
    if (s->sync_bitmap) {
        backup_cleanup_sync_bitmap(s, 0);
    }
}

static void backup_abort(Job *job)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common.job);
    if (s->sync_bitmap) {
        backup_cleanup_sync_bitmap(s, -1);
    }
}

static void backup_clean(Job *job)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common.job);

    block_copy_state_free(s->bcs);
}

void backup_do_checkpoint(BlockJob *job, Error **errp)
{
    BackupBlockJob *backup_job = container_of(job, BackupBlockJob, common);

    assert(block_job_driver(job) == &backup_job_driver);

    if (backup_job->sync_mode != MIRROR_SYNC_MODE_NONE) {
        error_setg(errp, "The backup job only supports block checkpoint in"
                   " sync=none mode");
        return;
    }

    bdrv_set_dirty_bitmap(backup_job->bcs->copy_bitmap, 0, backup_job->len);
}

static BlockErrorAction backup_error_action(BackupBlockJob *job,
                                            bool read, int error)
{
    if (read) {
        return block_job_error_action(&job->common, job->on_source_error,
                                      true, error);
    } else {
        return block_job_error_action(&job->common, job->on_target_error,
                                      false, error);
    }
}

static bool coroutine_fn yield_and_check(BackupBlockJob *job)
{
    uint64_t delay_ns;

    if (job_is_cancelled(&job->common.job)) {
        return true;
    }

    /* We need to yield even for delay_ns = 0 so that bdrv_drain_all() can
     * return. Without a yield, the VM would not reboot. */
    delay_ns = block_job_ratelimit_get_delay(&job->common, job->bytes_read);
    job->bytes_read = 0;
    job_sleep_ns(&job->common.job, delay_ns);

    if (job_is_cancelled(&job->common.job)) {
        return true;
    }

    return false;
}

static int coroutine_fn backup_loop(BackupBlockJob *job)
{
    bool error_is_read;
    int64_t offset;
    BdrvDirtyBitmapIter *bdbi;
    int ret = 0;

    bdbi = bdrv_dirty_iter_new(job->bcs->copy_bitmap);
    while ((offset = bdrv_dirty_iter_next(bdbi)) != -1) {
        do {
            if (yield_and_check(job)) {
                goto out;
            }
            ret = backup_do_cow(job, offset,
                                job->cluster_size, &error_is_read, false);
            if (ret < 0 && backup_error_action(job, error_is_read, -ret) ==
                           BLOCK_ERROR_ACTION_REPORT)
            {
                goto out;
            }
        } while (ret < 0);
    }

 out:
    bdrv_dirty_iter_free(bdbi);
    return ret;
}

static void backup_init_copy_bitmap(BackupBlockJob *job)
{
    bool ret;
    uint64_t estimate;

    if (job->sync_mode == MIRROR_SYNC_MODE_BITMAP) {
        ret = bdrv_dirty_bitmap_merge_internal(job->bcs->copy_bitmap,
                                               job->sync_bitmap,
                                               NULL, true);
        assert(ret);
    } else {
        if (job->sync_mode == MIRROR_SYNC_MODE_TOP) {
            /*
             * We can't hog the coroutine to initialize this thoroughly.
             * Set a flag and resume work when we are able to yield safely.
             */
            job->bcs->skip_unallocated = true;
        }
        bdrv_set_dirty_bitmap(job->bcs->copy_bitmap, 0, job->len);
    }

    estimate = bdrv_get_dirty_count(job->bcs->copy_bitmap);
    job_progress_set_remaining(&job->common.job, estimate);
}

static int coroutine_fn backup_run(Job *job, Error **errp)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common.job);
    int ret = 0;

    QLIST_INIT(&s->inflight_reqs);
    qemu_co_rwlock_init(&s->flush_rwlock);

    backup_init_copy_bitmap(s);

    s->before_write.notify = backup_before_write_notify;
    bdrv_add_before_write_notifier(s->source_bs, &s->before_write);

    if (s->sync_mode == MIRROR_SYNC_MODE_TOP) {
        int64_t offset = 0;
        int64_t count;

        for (offset = 0; offset < s->len; ) {
            if (yield_and_check(s)) {
                ret = -ECANCELED;
                goto out;
            }

            ret = block_copy_reset_unallocated(s->bcs, offset, &count);
            if (ret < 0) {
                goto out;
            }

            offset += count;
        }
        s->bcs->skip_unallocated = false;
    }

    if (s->sync_mode == MIRROR_SYNC_MODE_NONE) {
        /* All bits are set in copy_bitmap to allow any cluster to be copied.
         * This does not actually require them to be copied. */
        while (!job_is_cancelled(job)) {
            /* Yield until the job is cancelled.  We just let our before_write
             * notify callback service CoW requests. */
            job_yield(job);
        }
    } else {
        ret = backup_loop(s);
    }

 out:
    notifier_with_return_remove(&s->before_write);

    /* wait until pending backup_do_cow() calls have completed */
    qemu_co_rwlock_wrlock(&s->flush_rwlock);
    qemu_co_rwlock_unlock(&s->flush_rwlock);

    return ret;
}

static const BlockJobDriver backup_job_driver = {
    .job_driver = {
        .instance_size          = sizeof(BackupBlockJob),
        .job_type               = JOB_TYPE_BACKUP,
        .free                   = block_job_free,
        .user_resume            = block_job_user_resume,
        .run                    = backup_run,
        .commit                 = backup_commit,
        .abort                  = backup_abort,
        .clean                  = backup_clean,
    }
};

static int64_t backup_calculate_cluster_size(BlockDriverState *target,
                                             Error **errp)
{
    int ret;
    BlockDriverInfo bdi;

    /*
     * If there is no backing file on the target, we cannot rely on COW if our
     * backup cluster size is smaller than the target cluster size. Even for
     * targets with a backing file, try to avoid COW if possible.
     */
    ret = bdrv_get_info(target, &bdi);
    if (ret == -ENOTSUP && !target->backing) {
        /* Cluster size is not defined */
        warn_report("The target block device doesn't provide "
                    "information about the block size and it doesn't have a "
                    "backing file. The default block size of %u bytes is "
                    "used. If the actual block size of the target exceeds "
                    "this default, the backup may be unusable",
                    BACKUP_CLUSTER_SIZE_DEFAULT);
        return BACKUP_CLUSTER_SIZE_DEFAULT;
    } else if (ret < 0 && !target->backing) {
        error_setg_errno(errp, -ret,
            "Couldn't determine the cluster size of the target image, "
            "which has no backing file");
        error_append_hint(errp,
            "Aborting, since this may create an unusable destination image\n");
        return ret;
    } else if (ret < 0 && target->backing) {
        /* Not fatal; just trudge on ahead. */
        return BACKUP_CLUSTER_SIZE_DEFAULT;
    }

    return MAX(BACKUP_CLUSTER_SIZE_DEFAULT, bdi.cluster_size);
}

BlockJob *backup_job_create(const char *job_id, BlockDriverState *bs,
                  BlockDriverState *target, int64_t speed,
                  MirrorSyncMode sync_mode, BdrvDirtyBitmap *sync_bitmap,
                  BitmapSyncMode bitmap_mode,
                  bool compress,
                  BlockdevOnError on_source_error,
                  BlockdevOnError on_target_error,
                  int creation_flags,
                  BlockCompletionFunc *cb, void *opaque,
                  JobTxn *txn, Error **errp)
{
    int64_t len;
    BackupBlockJob *job = NULL;
    int64_t cluster_size;
    BdrvRequestFlags write_flags;

    assert(bs);
    assert(target);

    /* QMP interface protects us from these cases */
    assert(sync_mode != MIRROR_SYNC_MODE_INCREMENTAL);
    assert(sync_bitmap || sync_mode != MIRROR_SYNC_MODE_BITMAP);

    if (bs == target) {
        error_setg(errp, "Source and target cannot be the same");
        return NULL;
    }

    if (!bdrv_is_inserted(bs)) {
        error_setg(errp, "Device is not inserted: %s",
                   bdrv_get_device_name(bs));
        return NULL;
    }

    if (!bdrv_is_inserted(target)) {
        error_setg(errp, "Device is not inserted: %s",
                   bdrv_get_device_name(target));
        return NULL;
    }

    if (compress && !block_driver_can_compress(target->drv)) {
        error_setg(errp, "Compression is not supported for this drive %s",
                   bdrv_get_device_name(target));
        return NULL;
    }

    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_BACKUP_SOURCE, errp)) {
        return NULL;
    }

    if (bdrv_op_is_blocked(target, BLOCK_OP_TYPE_BACKUP_TARGET, errp)) {
        return NULL;
    }

    if (sync_bitmap) {
        /* If we need to write to this bitmap, check that we can: */
        if (bitmap_mode != BITMAP_SYNC_MODE_NEVER &&
            bdrv_dirty_bitmap_check(sync_bitmap, BDRV_BITMAP_DEFAULT, errp)) {
            return NULL;
        }

        /* Create a new bitmap, and freeze/disable this one. */
        if (bdrv_dirty_bitmap_create_successor(bs, sync_bitmap, errp) < 0) {
            return NULL;
        }
    }

    len = bdrv_getlength(bs);
    if (len < 0) {
        error_setg_errno(errp, -len, "unable to get length for '%s'",
                         bdrv_get_device_name(bs));
        goto error;
    }

    cluster_size = backup_calculate_cluster_size(target, errp);
    if (cluster_size < 0) {
        goto error;
    }

    /* job->len is fixed, so we can't allow resize */
    job = block_job_create(job_id, &backup_job_driver, txn, bs, 0, BLK_PERM_ALL,
                           speed, creation_flags, cb, opaque, errp);
    if (!job) {
        goto error;
    }

    job->source_bs = bs;
    job->on_source_error = on_source_error;
    job->on_target_error = on_target_error;
    job->sync_mode = sync_mode;
    job->sync_bitmap = sync_bitmap;
    job->bitmap_mode = bitmap_mode;

    /*
     * If source is in backing chain of target assume that target is going to be
     * used for "image fleecing", i.e. it should represent a kind of snapshot of
     * source at backup-start point in time. And target is going to be read by
     * somebody (for example, used as NBD export) during backup job.
     *
     * In this case, we need to add BDRV_REQ_SERIALISING write flag to avoid
     * intersection of backup writes and third party reads from target,
     * otherwise reading from target we may occasionally read already updated by
     * guest data.
     *
     * For more information see commit f8d59dfb40bb and test
     * tests/qemu-iotests/222
     */
    write_flags = (bdrv_chain_contains(target, bs) ? BDRV_REQ_SERIALISING : 0) |
                  (compress ? BDRV_REQ_WRITE_COMPRESSED : 0),

    job->bcs = block_copy_state_new(bs, target, cluster_size, write_flags,
                                    backup_progress_bytes_callback,
                                    backup_progress_reset_callback, job, errp);
    if (!job->bcs) {
        goto error;
    }

    job->cluster_size = cluster_size;

    /* Required permissions are already taken by block-copy-state target */
    block_job_add_bdrv(&job->common, "target", target, 0, BLK_PERM_ALL,
                       &error_abort);
    job->len = len;

    return &job->common;

 error:
    if (sync_bitmap) {
        bdrv_reclaim_dirty_bitmap(bs, sync_bitmap, NULL);
    }
    if (job) {
        backup_clean(&job->common.job);
        job_early_fail(&job->common.job);
    }

    return NULL;
}
