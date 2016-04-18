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
#include "block/blockjob.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/ratelimit.h"
#include "qemu/cutils.h"
#include "sysemu/block-backend.h"
#include "qemu/bitmap.h"

#define BACKUP_CLUSTER_SIZE_DEFAULT (1 << 16)
#define SLICE_TIME 100000000ULL /* ns */

typedef struct CowRequest {
    int64_t start;
    int64_t end;
    QLIST_ENTRY(CowRequest) list;
    CoQueue wait_queue; /* coroutines blocked on this request */
} CowRequest;

typedef struct BackupBlockJob {
    BlockJob common;
    BlockDriverState *target;
    /* bitmap for sync=incremental */
    BdrvDirtyBitmap *sync_bitmap;
    MirrorSyncMode sync_mode;
    RateLimit limit;
    BlockdevOnError on_source_error;
    BlockdevOnError on_target_error;
    CoRwlock flush_rwlock;
    uint64_t sectors_read;
    unsigned long *done_bitmap;
    int64_t cluster_size;
    QLIST_HEAD(, CowRequest) inflight_reqs;
} BackupBlockJob;

/* Size of a cluster in sectors, instead of bytes. */
static inline int64_t cluster_size_sectors(BackupBlockJob *job)
{
  return job->cluster_size / BDRV_SECTOR_SIZE;
}

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
            if (end > req->start && start < req->end) {
                qemu_co_queue_wait(&req->wait_queue);
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
    req->start = start;
    req->end = end;
    qemu_co_queue_init(&req->wait_queue);
    QLIST_INSERT_HEAD(&job->inflight_reqs, req, list);
}

/* Forget about a completed request */
static void cow_request_end(CowRequest *req)
{
    QLIST_REMOVE(req, list);
    qemu_co_queue_restart_all(&req->wait_queue);
}

static int coroutine_fn backup_do_cow(BlockDriverState *bs,
                                      int64_t sector_num, int nb_sectors,
                                      bool *error_is_read,
                                      bool is_write_notifier)
{
    BackupBlockJob *job = (BackupBlockJob *)bs->job;
    CowRequest cow_request;
    struct iovec iov;
    QEMUIOVector bounce_qiov;
    void *bounce_buffer = NULL;
    int ret = 0;
    int64_t sectors_per_cluster = cluster_size_sectors(job);
    int64_t start, end;
    int n;

    qemu_co_rwlock_rdlock(&job->flush_rwlock);

    start = sector_num / sectors_per_cluster;
    end = DIV_ROUND_UP(sector_num + nb_sectors, sectors_per_cluster);

    trace_backup_do_cow_enter(job, start, sector_num, nb_sectors);

    wait_for_overlapping_requests(job, start, end);
    cow_request_begin(&cow_request, job, start, end);

    for (; start < end; start++) {
        if (test_bit(start, job->done_bitmap)) {
            trace_backup_do_cow_skip(job, start);
            continue; /* already copied */
        }

        trace_backup_do_cow_process(job, start);

        n = MIN(sectors_per_cluster,
                job->common.len / BDRV_SECTOR_SIZE -
                start * sectors_per_cluster);

        if (!bounce_buffer) {
            bounce_buffer = qemu_blockalign(bs, job->cluster_size);
        }
        iov.iov_base = bounce_buffer;
        iov.iov_len = n * BDRV_SECTOR_SIZE;
        qemu_iovec_init_external(&bounce_qiov, &iov, 1);

        if (is_write_notifier) {
            ret = bdrv_co_readv_no_serialising(bs,
                                           start * sectors_per_cluster,
                                           n, &bounce_qiov);
        } else {
            ret = bdrv_co_readv(bs, start * sectors_per_cluster, n,
                                &bounce_qiov);
        }
        if (ret < 0) {
            trace_backup_do_cow_read_fail(job, start, ret);
            if (error_is_read) {
                *error_is_read = true;
            }
            goto out;
        }

        if (buffer_is_zero(iov.iov_base, iov.iov_len)) {
            ret = bdrv_co_write_zeroes(job->target,
                                       start * sectors_per_cluster,
                                       n, BDRV_REQ_MAY_UNMAP);
        } else {
            ret = bdrv_co_writev(job->target,
                                 start * sectors_per_cluster, n,
                                 &bounce_qiov);
        }
        if (ret < 0) {
            trace_backup_do_cow_write_fail(job, start, ret);
            if (error_is_read) {
                *error_is_read = false;
            }
            goto out;
        }

        set_bit(start, job->done_bitmap);

        /* Publish progress, guest I/O counts as progress too.  Note that the
         * offset field is an opaque progress value, it is not a disk offset.
         */
        job->sectors_read += n;
        job->common.offset += n * BDRV_SECTOR_SIZE;
    }

out:
    if (bounce_buffer) {
        qemu_vfree(bounce_buffer);
    }

    cow_request_end(&cow_request);

    trace_backup_do_cow_return(job, sector_num, nb_sectors, ret);

    qemu_co_rwlock_unlock(&job->flush_rwlock);

    return ret;
}

static int coroutine_fn backup_before_write_notify(
        NotifierWithReturn *notifier,
        void *opaque)
{
    BdrvTrackedRequest *req = opaque;
    int64_t sector_num = req->offset >> BDRV_SECTOR_BITS;
    int nb_sectors = req->bytes >> BDRV_SECTOR_BITS;

    assert((req->offset & (BDRV_SECTOR_SIZE - 1)) == 0);
    assert((req->bytes & (BDRV_SECTOR_SIZE - 1)) == 0);

    return backup_do_cow(req->bs, sector_num, nb_sectors, NULL, true);
}

static void backup_set_speed(BlockJob *job, int64_t speed, Error **errp)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common);

    if (speed < 0) {
        error_setg(errp, QERR_INVALID_PARAMETER, "speed");
        return;
    }
    ratelimit_set_speed(&s->limit, speed / BDRV_SECTOR_SIZE, SLICE_TIME);
}

static void backup_cleanup_sync_bitmap(BackupBlockJob *job, int ret)
{
    BdrvDirtyBitmap *bm;
    BlockDriverState *bs = job->common.bs;

    if (ret < 0 || block_job_is_cancelled(&job->common)) {
        /* Merge the successor back into the parent, delete nothing. */
        bm = bdrv_reclaim_dirty_bitmap(bs, job->sync_bitmap, NULL);
        assert(bm);
    } else {
        /* Everything is fine, delete this bitmap and install the backup. */
        bm = bdrv_dirty_bitmap_abdicate(bs, job->sync_bitmap, NULL);
        assert(bm);
    }
}

static void backup_commit(BlockJob *job)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common);
    if (s->sync_bitmap) {
        backup_cleanup_sync_bitmap(s, 0);
    }
}

static void backup_abort(BlockJob *job)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common);
    if (s->sync_bitmap) {
        backup_cleanup_sync_bitmap(s, -1);
    }
}

static const BlockJobDriver backup_job_driver = {
    .instance_size  = sizeof(BackupBlockJob),
    .job_type       = BLOCK_JOB_TYPE_BACKUP,
    .set_speed      = backup_set_speed,
    .commit         = backup_commit,
    .abort          = backup_abort,
};

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

typedef struct {
    int ret;
} BackupCompleteData;

static void backup_complete(BlockJob *job, void *opaque)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common);
    BackupCompleteData *data = opaque;

    bdrv_unref(s->target);

    block_job_completed(job, data->ret);
    g_free(data);
}

static bool coroutine_fn yield_and_check(BackupBlockJob *job)
{
    if (block_job_is_cancelled(&job->common)) {
        return true;
    }

    /* we need to yield so that bdrv_drain_all() returns.
     * (without, VM does not reboot)
     */
    if (job->common.speed) {
        uint64_t delay_ns = ratelimit_calculate_delay(&job->limit,
                                                      job->sectors_read);
        job->sectors_read = 0;
        block_job_sleep_ns(&job->common, QEMU_CLOCK_REALTIME, delay_ns);
    } else {
        block_job_sleep_ns(&job->common, QEMU_CLOCK_REALTIME, 0);
    }

    if (block_job_is_cancelled(&job->common)) {
        return true;
    }

    return false;
}

static int coroutine_fn backup_run_incremental(BackupBlockJob *job)
{
    bool error_is_read;
    int ret = 0;
    int clusters_per_iter;
    uint32_t granularity;
    int64_t sector;
    int64_t cluster;
    int64_t end;
    int64_t last_cluster = -1;
    int64_t sectors_per_cluster = cluster_size_sectors(job);
    BlockDriverState *bs = job->common.bs;
    HBitmapIter hbi;

    granularity = bdrv_dirty_bitmap_granularity(job->sync_bitmap);
    clusters_per_iter = MAX((granularity / job->cluster_size), 1);
    bdrv_dirty_iter_init(job->sync_bitmap, &hbi);

    /* Find the next dirty sector(s) */
    while ((sector = hbitmap_iter_next(&hbi)) != -1) {
        cluster = sector / sectors_per_cluster;

        /* Fake progress updates for any clusters we skipped */
        if (cluster != last_cluster + 1) {
            job->common.offset += ((cluster - last_cluster - 1) *
                                   job->cluster_size);
        }

        for (end = cluster + clusters_per_iter; cluster < end; cluster++) {
            do {
                if (yield_and_check(job)) {
                    return ret;
                }
                ret = backup_do_cow(bs, cluster * sectors_per_cluster,
                                    sectors_per_cluster, &error_is_read,
                                    false);
                if ((ret < 0) &&
                    backup_error_action(job, error_is_read, -ret) ==
                    BLOCK_ERROR_ACTION_REPORT) {
                    return ret;
                }
            } while (ret < 0);
        }

        /* If the bitmap granularity is smaller than the backup granularity,
         * we need to advance the iterator pointer to the next cluster. */
        if (granularity < job->cluster_size) {
            bdrv_set_dirty_iter(&hbi, cluster * sectors_per_cluster);
        }

        last_cluster = cluster - 1;
    }

    /* Play some final catchup with the progress meter */
    end = DIV_ROUND_UP(job->common.len, job->cluster_size);
    if (last_cluster + 1 < end) {
        job->common.offset += ((end - last_cluster - 1) * job->cluster_size);
    }

    return ret;
}

static void coroutine_fn backup_run(void *opaque)
{
    BackupBlockJob *job = opaque;
    BackupCompleteData *data;
    BlockDriverState *bs = job->common.bs;
    BlockDriverState *target = job->target;
    NotifierWithReturn before_write = {
        .notify = backup_before_write_notify,
    };
    int64_t start, end;
    int64_t sectors_per_cluster = cluster_size_sectors(job);
    int ret = 0;

    QLIST_INIT(&job->inflight_reqs);
    qemu_co_rwlock_init(&job->flush_rwlock);

    start = 0;
    end = DIV_ROUND_UP(job->common.len, job->cluster_size);

    job->done_bitmap = bitmap_new(end);

    bdrv_add_before_write_notifier(bs, &before_write);

    if (job->sync_mode == MIRROR_SYNC_MODE_NONE) {
        while (!block_job_is_cancelled(&job->common)) {
            /* Yield until the job is cancelled.  We just let our before_write
             * notify callback service CoW requests. */
            job->common.busy = false;
            qemu_coroutine_yield();
            job->common.busy = true;
        }
    } else if (job->sync_mode == MIRROR_SYNC_MODE_INCREMENTAL) {
        ret = backup_run_incremental(job);
    } else {
        /* Both FULL and TOP SYNC_MODE's require copying.. */
        for (; start < end; start++) {
            bool error_is_read;
            if (yield_and_check(job)) {
                break;
            }

            if (job->sync_mode == MIRROR_SYNC_MODE_TOP) {
                int i, n;
                int alloced = 0;

                /* Check to see if these blocks are already in the
                 * backing file. */

                for (i = 0; i < sectors_per_cluster;) {
                    /* bdrv_is_allocated() only returns true/false based
                     * on the first set of sectors it comes across that
                     * are are all in the same state.
                     * For that reason we must verify each sector in the
                     * backup cluster length.  We end up copying more than
                     * needed but at some point that is always the case. */
                    alloced =
                        bdrv_is_allocated(bs,
                                start * sectors_per_cluster + i,
                                sectors_per_cluster - i, &n);
                    i += n;

                    if (alloced == 1 || n == 0) {
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
            ret = backup_do_cow(bs, start * sectors_per_cluster,
                                sectors_per_cluster, &error_is_read, false);
            if (ret < 0) {
                /* Depending on error action, fail now or retry cluster */
                BlockErrorAction action =
                    backup_error_action(job, error_is_read, -ret);
                if (action == BLOCK_ERROR_ACTION_REPORT) {
                    break;
                } else {
                    start--;
                    continue;
                }
            }
        }
    }

    notifier_with_return_remove(&before_write);

    /* wait until pending backup_do_cow() calls have completed */
    qemu_co_rwlock_wrlock(&job->flush_rwlock);
    qemu_co_rwlock_unlock(&job->flush_rwlock);
    g_free(job->done_bitmap);

    bdrv_op_unblock_all(target, job->common.blocker);

    data = g_malloc(sizeof(*data));
    data->ret = ret;
    block_job_defer_to_main_loop(&job->common, backup_complete, data);
}

void backup_start(BlockDriverState *bs, BlockDriverState *target,
                  int64_t speed, MirrorSyncMode sync_mode,
                  BdrvDirtyBitmap *sync_bitmap,
                  BlockdevOnError on_source_error,
                  BlockdevOnError on_target_error,
                  BlockCompletionFunc *cb, void *opaque,
                  BlockJobTxn *txn, Error **errp)
{
    int64_t len;
    BlockDriverInfo bdi;
    int ret;

    assert(bs);
    assert(target);
    assert(cb);

    if (bs == target) {
        error_setg(errp, "Source and target cannot be the same");
        return;
    }

    if (!bdrv_is_inserted(bs)) {
        error_setg(errp, "Device is not inserted: %s",
                   bdrv_get_device_name(bs));
        return;
    }

    if (!bdrv_is_inserted(target)) {
        error_setg(errp, "Device is not inserted: %s",
                   bdrv_get_device_name(target));
        return;
    }

    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_BACKUP_SOURCE, errp)) {
        return;
    }

    if (bdrv_op_is_blocked(target, BLOCK_OP_TYPE_BACKUP_TARGET, errp)) {
        return;
    }

    if (sync_mode == MIRROR_SYNC_MODE_INCREMENTAL) {
        if (!sync_bitmap) {
            error_setg(errp, "must provide a valid bitmap name for "
                             "\"incremental\" sync mode");
            return;
        }

        /* Create a new bitmap, and freeze/disable this one. */
        if (bdrv_dirty_bitmap_create_successor(bs, sync_bitmap, errp) < 0) {
            return;
        }
    } else if (sync_bitmap) {
        error_setg(errp,
                   "a sync_bitmap was provided to backup_run, "
                   "but received an incompatible sync_mode (%s)",
                   MirrorSyncMode_lookup[sync_mode]);
        return;
    }

    len = bdrv_getlength(bs);
    if (len < 0) {
        error_setg_errno(errp, -len, "unable to get length for '%s'",
                         bdrv_get_device_name(bs));
        goto error;
    }

    BackupBlockJob *job = block_job_create(&backup_job_driver, bs, speed,
                                           cb, opaque, errp);
    if (!job) {
        goto error;
    }

    job->on_source_error = on_source_error;
    job->on_target_error = on_target_error;
    job->target = target;
    job->sync_mode = sync_mode;
    job->sync_bitmap = sync_mode == MIRROR_SYNC_MODE_INCREMENTAL ?
                       sync_bitmap : NULL;

    /* If there is no backing file on the target, we cannot rely on COW if our
     * backup cluster size is smaller than the target cluster size. Even for
     * targets with a backing file, try to avoid COW if possible. */
    ret = bdrv_get_info(job->target, &bdi);
    if (ret < 0 && !target->backing) {
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

    bdrv_op_block_all(target, job->common.blocker);
    job->common.len = len;
    job->common.co = qemu_coroutine_create(backup_run);
    block_job_txn_add_job(txn, &job->common);
    qemu_coroutine_enter(job->common.co, job);
    return;

 error:
    if (sync_bitmap) {
        bdrv_reclaim_dirty_bitmap(bs, sync_bitmap, NULL);
    }
}
