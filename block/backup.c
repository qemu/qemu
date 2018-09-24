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

typedef struct BackupBlockJob {
    BlockJob common;
    BlockBackend *target;
    /* bitmap for sync=incremental */
    BdrvDirtyBitmap *sync_bitmap;
    MirrorSyncMode sync_mode;
    BlockdevOnError on_source_error;
    BlockdevOnError on_target_error;
    CoRwlock flush_rwlock;
    uint64_t len;
    uint64_t bytes_read;
    int64_t cluster_size;
    bool compress;
    NotifierWithReturn before_write;
    QLIST_HEAD(, CowRequest) inflight_reqs;

    HBitmap *copy_bitmap;
    bool use_copy_range;
    int64_t copy_range_size;

    bool serialize_target_writes;
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
    struct iovec iov;
    QEMUIOVector qiov;
    BlockBackend *blk = job->common.blk;
    int nbytes;
    int read_flags = is_write_notifier ? BDRV_REQ_NO_SERIALISING : 0;
    int write_flags = job->serialize_target_writes ? BDRV_REQ_SERIALISING : 0;

    hbitmap_reset(job->copy_bitmap, start / job->cluster_size, 1);
    nbytes = MIN(job->cluster_size, job->len - start);
    if (!*bounce_buffer) {
        *bounce_buffer = blk_blockalign(blk, job->cluster_size);
    }
    iov.iov_base = *bounce_buffer;
    iov.iov_len = nbytes;
    qemu_iovec_init_external(&qiov, &iov, 1);

    ret = blk_co_preadv(blk, start, qiov.size, &qiov, read_flags);
    if (ret < 0) {
        trace_backup_do_cow_read_fail(job, start, ret);
        if (error_is_read) {
            *error_is_read = true;
        }
        goto fail;
    }

    if (qemu_iovec_is_zero(&qiov)) {
        ret = blk_co_pwrite_zeroes(job->target, start,
                                   qiov.size, write_flags | BDRV_REQ_MAY_UNMAP);
    } else {
        ret = blk_co_pwritev(job->target, start,
                             qiov.size, &qiov, write_flags |
                             (job->compress ? BDRV_REQ_WRITE_COMPRESSED : 0));
    }
    if (ret < 0) {
        trace_backup_do_cow_write_fail(job, start, ret);
        if (error_is_read) {
            *error_is_read = false;
        }
        goto fail;
    }

    return nbytes;
fail:
    hbitmap_set(job->copy_bitmap, start / job->cluster_size, 1);
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
    int write_flags = job->serialize_target_writes ? BDRV_REQ_SERIALISING : 0;

    assert(QEMU_IS_ALIGNED(job->copy_range_size, job->cluster_size));
    nbytes = MIN(job->copy_range_size, end - start);
    nr_clusters = DIV_ROUND_UP(nbytes, job->cluster_size);
    hbitmap_reset(job->copy_bitmap, start / job->cluster_size,
                  nr_clusters);
    ret = blk_co_copy_range(blk, start, job->target, start, nbytes,
                            read_flags, write_flags);
    if (ret < 0) {
        trace_backup_do_cow_copy_range_fail(job, start, ret);
        hbitmap_set(job->copy_bitmap, start / job->cluster_size,
                    nr_clusters);
        return ret;
    }

    return nbytes;
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

    qemu_co_rwlock_rdlock(&job->flush_rwlock);

    start = QEMU_ALIGN_DOWN(offset, job->cluster_size);
    end = QEMU_ALIGN_UP(bytes + offset, job->cluster_size);

    trace_backup_do_cow_enter(job, start, offset, bytes);

    wait_for_overlapping_requests(job, start, end);
    cow_request_begin(&cow_request, job, start, end);

    while (start < end) {
        if (!hbitmap_get(job->copy_bitmap, start / job->cluster_size)) {
            trace_backup_do_cow_skip(job, start);
            start += job->cluster_size;
            continue; /* already copied */
        }

        trace_backup_do_cow_process(job, start);

        if (job->use_copy_range) {
            ret = backup_cow_with_offload(job, start, end, is_write_notifier);
            if (ret < 0) {
                job->use_copy_range = false;
            }
        }
        if (!job->use_copy_range) {
            ret = backup_cow_with_bounce_buffer(job, start, end, is_write_notifier,
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

    if (ret < 0) {
        /* Merge the successor back into the parent, delete nothing. */
        bm = bdrv_reclaim_dirty_bitmap(bs, job->sync_bitmap, NULL);
        assert(bm);
    } else {
        /* Everything is fine, delete this bitmap and install the backup. */
        bm = bdrv_dirty_bitmap_abdicate(bs, job->sync_bitmap, NULL);
        assert(bm);
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
    assert(s->target);
    blk_unref(s->target);
    s->target = NULL;
}

static void backup_attached_aio_context(BlockJob *job, AioContext *aio_context)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common);

    blk_set_aio_context(s->target, aio_context);
}

void backup_do_checkpoint(BlockJob *job, Error **errp)
{
    BackupBlockJob *backup_job = container_of(job, BackupBlockJob, common);
    int64_t len;

    assert(block_job_driver(job) == &backup_job_driver);

    if (backup_job->sync_mode != MIRROR_SYNC_MODE_NONE) {
        error_setg(errp, "The backup job only supports block checkpoint in"
                   " sync=none mode");
        return;
    }

    len = DIV_ROUND_UP(backup_job->len, backup_job->cluster_size);
    hbitmap_set(backup_job->copy_bitmap, 0, len);
}

void backup_wait_for_overlapping_requests(BlockJob *job, int64_t offset,
                                          uint64_t bytes)
{
    BackupBlockJob *backup_job = container_of(job, BackupBlockJob, common);
    int64_t start, end;

    assert(block_job_driver(job) == &backup_job_driver);

    start = QEMU_ALIGN_DOWN(offset, backup_job->cluster_size);
    end = QEMU_ALIGN_UP(offset + bytes, backup_job->cluster_size);
    wait_for_overlapping_requests(backup_job, start, end);
}

void backup_cow_request_begin(CowRequest *req, BlockJob *job,
                              int64_t offset, uint64_t bytes)
{
    BackupBlockJob *backup_job = container_of(job, BackupBlockJob, common);
    int64_t start, end;

    assert(block_job_driver(job) == &backup_job_driver);

    start = QEMU_ALIGN_DOWN(offset, backup_job->cluster_size);
    end = QEMU_ALIGN_UP(offset + bytes, backup_job->cluster_size);
    cow_request_begin(req, backup_job, start, end);
}

void backup_cow_request_end(CowRequest *req)
{
    cow_request_end(req);
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

static int coroutine_fn backup_run_incremental(BackupBlockJob *job)
{
    int ret;
    bool error_is_read;
    int64_t cluster;
    HBitmapIter hbi;

    hbitmap_iter_init(&hbi, job->copy_bitmap, 0);
    while ((cluster = hbitmap_iter_next(&hbi, true)) != -1) {
        do {
            if (yield_and_check(job)) {
                return 0;
            }
            ret = backup_do_cow(job, cluster * job->cluster_size,
                                job->cluster_size, &error_is_read, false);
            if (ret < 0 && backup_error_action(job, error_is_read, -ret) ==
                           BLOCK_ERROR_ACTION_REPORT)
            {
                return ret;
            }
        } while (ret < 0);
    }

    return 0;
}

/* init copy_bitmap from sync_bitmap */
static void backup_incremental_init_copy_bitmap(BackupBlockJob *job)
{
    BdrvDirtyBitmapIter *dbi;
    int64_t offset;
    int64_t end = DIV_ROUND_UP(bdrv_dirty_bitmap_size(job->sync_bitmap),
                               job->cluster_size);

    dbi = bdrv_dirty_iter_new(job->sync_bitmap);
    while ((offset = bdrv_dirty_iter_next(dbi)) != -1) {
        int64_t cluster = offset / job->cluster_size;
        int64_t next_cluster;

        offset += bdrv_dirty_bitmap_granularity(job->sync_bitmap);
        if (offset >= bdrv_dirty_bitmap_size(job->sync_bitmap)) {
            hbitmap_set(job->copy_bitmap, cluster, end - cluster);
            break;
        }

        offset = bdrv_dirty_bitmap_next_zero(job->sync_bitmap, offset);
        if (offset == -1) {
            hbitmap_set(job->copy_bitmap, cluster, end - cluster);
            break;
        }

        next_cluster = DIV_ROUND_UP(offset, job->cluster_size);
        hbitmap_set(job->copy_bitmap, cluster, next_cluster - cluster);
        if (next_cluster >= end) {
            break;
        }

        bdrv_set_dirty_iter(dbi, next_cluster * job->cluster_size);
    }

    /* TODO job_progress_set_remaining() would make more sense */
    job_progress_update(&job->common.job,
        job->len - hbitmap_count(job->copy_bitmap) * job->cluster_size);

    bdrv_dirty_iter_free(dbi);
}

static int coroutine_fn backup_run(Job *job, Error **errp)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common.job);
    BlockDriverState *bs = blk_bs(s->common.blk);
    int64_t offset, nb_clusters;
    int ret = 0;

    QLIST_INIT(&s->inflight_reqs);
    qemu_co_rwlock_init(&s->flush_rwlock);

    nb_clusters = DIV_ROUND_UP(s->len, s->cluster_size);
    job_progress_set_remaining(job, s->len);

    s->copy_bitmap = hbitmap_alloc(nb_clusters, 0);
    if (s->sync_mode == MIRROR_SYNC_MODE_INCREMENTAL) {
        backup_incremental_init_copy_bitmap(s);
    } else {
        hbitmap_set(s->copy_bitmap, 0, nb_clusters);
    }


    s->before_write.notify = backup_before_write_notify;
    bdrv_add_before_write_notifier(bs, &s->before_write);

    if (s->sync_mode == MIRROR_SYNC_MODE_NONE) {
        /* All bits are set in copy_bitmap to allow any cluster to be copied.
         * This does not actually require them to be copied. */
        while (!job_is_cancelled(job)) {
            /* Yield until the job is cancelled.  We just let our before_write
             * notify callback service CoW requests. */
            job_yield(job);
        }
    } else if (s->sync_mode == MIRROR_SYNC_MODE_INCREMENTAL) {
        ret = backup_run_incremental(s);
    } else {
        /* Both FULL and TOP SYNC_MODE's require copying.. */
        for (offset = 0; offset < s->len;
             offset += s->cluster_size) {
            bool error_is_read;
            int alloced = 0;

            if (yield_and_check(s)) {
                break;
            }

            if (s->sync_mode == MIRROR_SYNC_MODE_TOP) {
                int i;
                int64_t n;

                /* Check to see if these blocks are already in the
                 * backing file. */

                for (i = 0; i < s->cluster_size;) {
                    /* bdrv_is_allocated() only returns true/false based
                     * on the first set of sectors it comes across that
                     * are are all in the same state.
                     * For that reason we must verify each sector in the
                     * backup cluster length.  We end up copying more than
                     * needed but at some point that is always the case. */
                    alloced =
                        bdrv_is_allocated(bs, offset + i,
                                          s->cluster_size - i, &n);
                    i += n;

                    if (alloced || n == 0) {
                        break;
                    }
                }

                /* If the above loop never found any sectors that are in
                 * the topmost image, skip this backup. */
                if (alloced == 0) {
                    continue;
                }
            }
            /* FULL sync mode we copy the whole drive. */
            if (alloced < 0) {
                ret = alloced;
            } else {
                ret = backup_do_cow(s, offset, s->cluster_size,
                                    &error_is_read, false);
            }
            if (ret < 0) {
                /* Depending on error action, fail now or retry cluster */
                BlockErrorAction action =
                    backup_error_action(s, error_is_read, -ret);
                if (action == BLOCK_ERROR_ACTION_REPORT) {
                    break;
                } else {
                    offset -= s->cluster_size;
                    continue;
                }
            }
        }
    }

    notifier_with_return_remove(&s->before_write);

    /* wait until pending backup_do_cow() calls have completed */
    qemu_co_rwlock_wrlock(&s->flush_rwlock);
    qemu_co_rwlock_unlock(&s->flush_rwlock);
    hbitmap_free(s->copy_bitmap);

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
    .attached_aio_context   = backup_attached_aio_context,
    .drain                  = backup_drain,
};

BlockJob *backup_job_create(const char *job_id, BlockDriverState *bs,
                  BlockDriverState *target, int64_t speed,
                  MirrorSyncMode sync_mode, BdrvDirtyBitmap *sync_bitmap,
                  bool compress,
                  BlockdevOnError on_source_error,
                  BlockdevOnError on_target_error,
                  int creation_flags,
                  BlockCompletionFunc *cb, void *opaque,
                  JobTxn *txn, Error **errp)
{
    int64_t len;
    BlockDriverInfo bdi;
    BackupBlockJob *job = NULL;
    int ret;

    assert(bs);
    assert(target);

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

    if (sync_mode == MIRROR_SYNC_MODE_INCREMENTAL) {
        if (!sync_bitmap) {
            error_setg(errp, "must provide a valid bitmap name for "
                             "\"incremental\" sync mode");
            return NULL;
        }

        /* Create a new bitmap, and freeze/disable this one. */
        if (bdrv_dirty_bitmap_create_successor(bs, sync_bitmap, errp) < 0) {
            return NULL;
        }
    } else if (sync_bitmap) {
        error_setg(errp,
                   "a sync_bitmap was provided to backup_run, "
                   "but received an incompatible sync_mode (%s)",
                   MirrorSyncMode_str(sync_mode));
        return NULL;
    }

    len = bdrv_getlength(bs);
    if (len < 0) {
        error_setg_errno(errp, -len, "unable to get length for '%s'",
                         bdrv_get_device_name(bs));
        goto error;
    }

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
    job->target = blk_new(BLK_PERM_WRITE,
                          BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE |
                          BLK_PERM_WRITE_UNCHANGED | BLK_PERM_GRAPH_MOD);
    ret = blk_insert_bs(job->target, target, errp);
    if (ret < 0) {
        goto error;
    }

    job->on_source_error = on_source_error;
    job->on_target_error = on_target_error;
    job->sync_mode = sync_mode;
    job->sync_bitmap = sync_mode == MIRROR_SYNC_MODE_INCREMENTAL ?
                       sync_bitmap : NULL;
    job->compress = compress;

    /* Detect image-fleecing (and similar) schemes */
    job->serialize_target_writes = bdrv_chain_contains(target, bs);

    /* If there is no backing file on the target, we cannot rely on COW if our
     * backup cluster size is smaller than the target cluster size. Even for
     * targets with a backing file, try to avoid COW if possible. */
    ret = bdrv_get_info(target, &bdi);
    if (ret == -ENOTSUP && !target->backing) {
        /* Cluster size is not defined */
        warn_report("The target block device doesn't provide "
                    "information about the block size and it doesn't have a "
                    "backing file. The default block size of %u bytes is "
                    "used. If the actual block size of the target exceeds "
                    "this default, the backup may be unusable",
                    BACKUP_CLUSTER_SIZE_DEFAULT);
        job->cluster_size = BACKUP_CLUSTER_SIZE_DEFAULT;
    } else if (ret < 0 && !target->backing) {
        error_setg_errno(errp, -ret,
            "Couldn't determine the cluster size of the target image, "
            "which has no backing file");
        error_append_hint(errp,
            "Aborting, since this may create an unusable destination image\n");
        goto error;
    } else if (ret < 0 && target->backing) {
        /* Not fatal; just trudge on ahead. */
        job->cluster_size = BACKUP_CLUSTER_SIZE_DEFAULT;
    } else {
        job->cluster_size = MAX(BACKUP_CLUSTER_SIZE_DEFAULT, bdi.cluster_size);
    }
    job->use_copy_range = true;
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
    if (sync_bitmap) {
        bdrv_reclaim_dirty_bitmap(bs, sync_bitmap, NULL);
    }
    if (job) {
        backup_clean(&job->common.job);
        job_early_fail(&job->common.job);
    }

    return NULL;
}
