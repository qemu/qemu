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

typedef struct BackupBlockJob {
    BlockJob common;
    BlockBackend *target;

    BdrvDirtyBitmap *sync_bitmap;
    BdrvDirtyBitmap *copy_bitmap;

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

    bool use_copy_range;
    int64_t copy_range_size;

    BdrvRequestFlags write_flags;
    bool initializing_bitmap;
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

/* Copy range to target with a bounce buffer and return the bytes copied. If
 * error occurred, return a negative error number */
static int coroutine_fn backup_cow_with_bounce_buffer(BackupBlockJob *job,
                                                      int64_t start,
                                                      int64_t end,
                                                      bool is_write_notifier,
                                                      bool *error_is_read,
                                                      void **bounce_buffer)
{
    int ret;
    BlockBackend *blk = job->common.blk;
    int nbytes;
    int read_flags = is_write_notifier ? BDRV_REQ_NO_SERIALISING : 0;

    assert(QEMU_IS_ALIGNED(start, job->cluster_size));
    bdrv_reset_dirty_bitmap(job->copy_bitmap, start, job->cluster_size);
    nbytes = MIN(job->cluster_size, job->len - start);
    if (!*bounce_buffer) {
        *bounce_buffer = blk_blockalign(blk, job->cluster_size);
    }

    ret = blk_co_pread(blk, start, nbytes, *bounce_buffer, read_flags);
    if (ret < 0) {
        trace_backup_do_cow_read_fail(job, start, ret);
        if (error_is_read) {
            *error_is_read = true;
        }
        goto fail;
    }

    ret = blk_co_pwrite(job->target, start, nbytes, *bounce_buffer,
                        job->write_flags);
    if (ret < 0) {
        trace_backup_do_cow_write_fail(job, start, ret);
        if (error_is_read) {
            *error_is_read = false;
        }
        goto fail;
    }

    return nbytes;
fail:
    bdrv_set_dirty_bitmap(job->copy_bitmap, start, job->cluster_size);
    return ret;

}

/* Copy range to target and return the bytes copied. If error occurred, return a
 * negative error number. */
static int coroutine_fn backup_cow_with_offload(BackupBlockJob *job,
                                                int64_t start,
                                                int64_t end,
                                                bool is_write_notifier)
{
    int ret;
    int nr_clusters;
    BlockBackend *blk = job->common.blk;
    int nbytes;
    int read_flags = is_write_notifier ? BDRV_REQ_NO_SERIALISING : 0;

    assert(QEMU_IS_ALIGNED(job->copy_range_size, job->cluster_size));
    assert(QEMU_IS_ALIGNED(start, job->cluster_size));
    nbytes = MIN(job->copy_range_size, end - start);
    nr_clusters = DIV_ROUND_UP(nbytes, job->cluster_size);
    bdrv_reset_dirty_bitmap(job->copy_bitmap, start,
                            job->cluster_size * nr_clusters);
    ret = blk_co_copy_range(blk, start, job->target, start, nbytes,
                            read_flags, job->write_flags);
    if (ret < 0) {
        trace_backup_do_cow_copy_range_fail(job, start, ret);
        bdrv_set_dirty_bitmap(job->copy_bitmap, start,
                              job->cluster_size * nr_clusters);
        return ret;
    }

    return nbytes;
}

/*
 * Check if the cluster starting at offset is allocated or not.
 * return via pnum the number of contiguous clusters sharing this allocation.
 */
static int backup_is_cluster_allocated(BackupBlockJob *s, int64_t offset,
                                       int64_t *pnum)
{
    BlockDriverState *bs = blk_bs(s->common.blk);
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
static int64_t backup_bitmap_reset_unallocated(BackupBlockJob *s,
                                               int64_t offset, int64_t *count)
{
    int ret;
    int64_t clusters, bytes, estimate;

    ret = backup_is_cluster_allocated(s, offset, &clusters);
    if (ret < 0) {
        return ret;
    }

    bytes = clusters * s->cluster_size;

    if (!ret) {
        bdrv_reset_dirty_bitmap(s->copy_bitmap, offset, bytes);
        estimate = bdrv_get_dirty_count(s->copy_bitmap);
        job_progress_set_remaining(&s->common.job, estimate);
    }

    *count = bytes;
    return ret;
}

static int coroutine_fn backup_do_cow(BackupBlockJob *job,
                                      int64_t offset, uint64_t bytes,
                                      bool *error_is_read,
                                      bool is_write_notifier)
{
    CowRequest cow_request;
    int ret = 0;
    int64_t start, end; /* bytes */
    void *bounce_buffer = NULL;
    int64_t status_bytes;

    qemu_co_rwlock_rdlock(&job->flush_rwlock);

    start = QEMU_ALIGN_DOWN(offset, job->cluster_size);
    end = QEMU_ALIGN_UP(bytes + offset, job->cluster_size);

    trace_backup_do_cow_enter(job, start, offset, bytes);

    wait_for_overlapping_requests(job, start, end);
    cow_request_begin(&cow_request, job, start, end);

    while (start < end) {
        int64_t dirty_end;

        if (!bdrv_dirty_bitmap_get(job->copy_bitmap, start)) {
            trace_backup_do_cow_skip(job, start);
            start += job->cluster_size;
            continue; /* already copied */
        }

        dirty_end = bdrv_dirty_bitmap_next_zero(job->copy_bitmap, start,
                                                (end - start));
        if (dirty_end < 0) {
            dirty_end = end;
        }

        if (job->initializing_bitmap) {
            ret = backup_bitmap_reset_unallocated(job, start, &status_bytes);
            if (ret == 0) {
                trace_backup_do_cow_skip_range(job, start, status_bytes);
                start += status_bytes;
                continue;
            }
            /* Clamp to known allocated region */
            dirty_end = MIN(dirty_end, start + status_bytes);
        }

        trace_backup_do_cow_process(job, start);

        if (job->use_copy_range) {
            ret = backup_cow_with_offload(job, start, dirty_end,
                                          is_write_notifier);
            if (ret < 0) {
                job->use_copy_range = false;
            }
        }
        if (!job->use_copy_range) {
            ret = backup_cow_with_bounce_buffer(job, start, dirty_end,
                                                is_write_notifier,
                                                error_is_read, &bounce_buffer);
        }
        if (ret < 0) {
            break;
        }

        /* Publish progress, guest I/O counts as progress too.  Note that the
         * offset field is an opaque progress value, it is not a disk offset.
         */
        start += ret;
        job->bytes_read += ret;
        job_progress_update(&job->common.job, ret);
        ret = 0;
    }

    if (bounce_buffer) {
        qemu_vfree(bounce_buffer);
    }

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

    assert(req->bs == blk_bs(job->common.blk));
    assert(QEMU_IS_ALIGNED(req->offset, BDRV_SECTOR_SIZE));
    assert(QEMU_IS_ALIGNED(req->bytes, BDRV_SECTOR_SIZE));

    return backup_do_cow(job, req->offset, req->bytes, NULL, true);
}

static void backup_cleanup_sync_bitmap(BackupBlockJob *job, int ret)
{
    BdrvDirtyBitmap *bm;
    BlockDriverState *bs = blk_bs(job->common.blk);
    bool sync = (((ret == 0) || (job->bitmap_mode == BITMAP_SYNC_MODE_ALWAYS)) \
                 && (job->bitmap_mode != BITMAP_SYNC_MODE_NEVER));

    if (sync) {
        /*
         * We succeeded, or we always intended to sync the bitmap.
         * Delete this bitmap and install the child.
         */
        bm = bdrv_dirty_bitmap_abdicate(bs, job->sync_bitmap, NULL);
    } else {
        /*
         * We failed, or we never intended to sync the bitmap anyway.
         * Merge the successor back into the parent, keeping all data.
         */
        bm = bdrv_reclaim_dirty_bitmap(bs, job->sync_bitmap, NULL);
    }

    assert(bm);

    if (ret < 0 && job->bitmap_mode == BITMAP_SYNC_MODE_ALWAYS) {
        /* If we failed and synced, merge in the bits we didn't copy: */
        bdrv_dirty_bitmap_merge_internal(bm, job->copy_bitmap,
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
    BlockDriverState *bs = blk_bs(s->common.blk);

    if (s->copy_bitmap) {
        bdrv_release_dirty_bitmap(bs, s->copy_bitmap);
        s->copy_bitmap = NULL;
    }

    assert(s->target);
    blk_unref(s->target);
    s->target = NULL;
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

    bdrv_set_dirty_bitmap(backup_job->copy_bitmap, 0, backup_job->len);
}

static void backup_drain(BlockJob *job)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common);

    /* Need to keep a reference in case blk_drain triggers execution
     * of backup_complete...
     */
    if (s->target) {
        BlockBackend *target = s->target;
        blk_ref(target);
        blk_drain(target);
        blk_unref(target);
    }
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

    bdbi = bdrv_dirty_iter_new(job->copy_bitmap);
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
        ret = bdrv_dirty_bitmap_merge_internal(job->copy_bitmap,
                                               job->sync_bitmap,
                                               NULL, true);
        assert(ret);
    } else {
        if (job->sync_mode == MIRROR_SYNC_MODE_TOP) {
            /*
             * We can't hog the coroutine to initialize this thoroughly.
             * Set a flag and resume work when we are able to yield safely.
             */
            job->initializing_bitmap = true;
        }
        bdrv_set_dirty_bitmap(job->copy_bitmap, 0, job->len);
    }

    estimate = bdrv_get_dirty_count(job->copy_bitmap);
    job_progress_set_remaining(&job->common.job, estimate);
}

static int coroutine_fn backup_run(Job *job, Error **errp)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common.job);
    BlockDriverState *bs = blk_bs(s->common.blk);
    int ret = 0;

    QLIST_INIT(&s->inflight_reqs);
    qemu_co_rwlock_init(&s->flush_rwlock);

    backup_init_copy_bitmap(s);

    s->before_write.notify = backup_before_write_notify;
    bdrv_add_before_write_notifier(bs, &s->before_write);

    if (s->sync_mode == MIRROR_SYNC_MODE_TOP) {
        int64_t offset = 0;
        int64_t count;

        for (offset = 0; offset < s->len; ) {
            if (yield_and_check(s)) {
                ret = -ECANCELED;
                goto out;
            }

            ret = backup_bitmap_reset_unallocated(s, offset, &count);
            if (ret < 0) {
                goto out;
            }

            offset += count;
        }
        s->initializing_bitmap = false;
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
        .drain                  = block_job_drain,
        .run                    = backup_run,
        .commit                 = backup_commit,
        .abort                  = backup_abort,
        .clean                  = backup_clean,
    },
    .drain                  = backup_drain,
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
    int ret;
    int64_t cluster_size;
    BdrvDirtyBitmap *copy_bitmap = NULL;

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

    if (compress && target->drv->bdrv_co_pwritev_compressed == NULL) {
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

    copy_bitmap = bdrv_create_dirty_bitmap(bs, cluster_size, NULL, errp);
    if (!copy_bitmap) {
        goto error;
    }
    bdrv_disable_dirty_bitmap(copy_bitmap);

    /* job->len is fixed, so we can't allow resize */
    job = block_job_create(job_id, &backup_job_driver, txn, bs,
                           BLK_PERM_CONSISTENT_READ,
                           BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE |
                           BLK_PERM_WRITE_UNCHANGED | BLK_PERM_GRAPH_MOD,
                           speed, creation_flags, cb, opaque, errp);
    if (!job) {
        goto error;
    }

    /* The target must match the source in size, so no resize here either */
    job->target = blk_new(job->common.job.aio_context,
                          BLK_PERM_WRITE,
                          BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE |
                          BLK_PERM_WRITE_UNCHANGED | BLK_PERM_GRAPH_MOD);
    ret = blk_insert_bs(job->target, target, errp);
    if (ret < 0) {
        goto error;
    }
    blk_set_disable_request_queuing(job->target, true);

    job->on_source_error = on_source_error;
    job->on_target_error = on_target_error;
    job->sync_mode = sync_mode;
    job->sync_bitmap = sync_bitmap;
    job->bitmap_mode = bitmap_mode;

    /*
     * Set write flags:
     * 1. Detect image-fleecing (and similar) schemes
     * 2. Handle compression
     */
    job->write_flags =
        (bdrv_chain_contains(target, bs) ? BDRV_REQ_SERIALISING : 0) |
        (compress ? BDRV_REQ_WRITE_COMPRESSED : 0);

    job->cluster_size = cluster_size;
    job->copy_bitmap = copy_bitmap;
    copy_bitmap = NULL;
    job->use_copy_range = !compress; /* compression isn't supported for it */
    job->copy_range_size = MIN_NON_ZERO(blk_get_max_transfer(job->common.blk),
                                        blk_get_max_transfer(job->target));
    job->copy_range_size = MAX(job->cluster_size,
                               QEMU_ALIGN_UP(job->copy_range_size,
                                             job->cluster_size));

    /* Required permissions are already taken with target's blk_new() */
    block_job_add_bdrv(&job->common, "target", target, 0, BLK_PERM_ALL,
                       &error_abort);
    job->len = len;

    return &job->common;

 error:
    if (copy_bitmap) {
        assert(!job || !job->copy_bitmap);
        bdrv_release_dirty_bitmap(bs, copy_bitmap);
    }
    if (sync_bitmap) {
        bdrv_reclaim_dirty_bitmap(bs, sync_bitmap, NULL);
    }
    if (job) {
        backup_clean(&job->common.job);
        job_early_fail(&job->common.job);
    }

    return NULL;
}
