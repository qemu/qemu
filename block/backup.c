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
#define SLICE_TIME 100000000ULL /* ns */

typedef struct BackupBlockJob {
    BlockJob common;
    BlockBackend *target;
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
    bool compress;
    NotifierWithReturn before_write;
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

static int coroutine_fn backup_do_cow(BackupBlockJob *job,
                                      int64_t sector_num, int nb_sectors,
                                      bool *error_is_read,
                                      bool is_write_notifier)
{
    BlockBackend *blk = job->common.blk;
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
            bounce_buffer = blk_blockalign(blk, job->cluster_size);
        }
        iov.iov_base = bounce_buffer;
        iov.iov_len = n * BDRV_SECTOR_SIZE;
        qemu_iovec_init_external(&bounce_qiov, &iov, 1);

        ret = blk_co_preadv(blk, start * job->cluster_size,
                            bounce_qiov.size, &bounce_qiov,
                            is_write_notifier ? BDRV_REQ_NO_SERIALISING : 0);
        if (ret < 0) {
            trace_backup_do_cow_read_fail(job, start, ret);
            if (error_is_read) {
                *error_is_read = true;
            }
            goto out;
        }

        if (buffer_is_zero(iov.iov_base, iov.iov_len)) {
            ret = blk_co_pwrite_zeroes(job->target, start * job->cluster_size,
                                       bounce_qiov.size, BDRV_REQ_MAY_UNMAP);
        } else {
            ret = blk_co_pwritev(job->target, start * job->cluster_size,
                                 bounce_qiov.size, &bounce_qiov,
                                 job->compress ? BDRV_REQ_WRITE_COMPRESSED : 0);
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
    BackupBlockJob *job = container_of(notifier, BackupBlockJob, before_write);
    BdrvTrackedRequest *req = opaque;
    int64_t sector_num = req->offset >> BDRV_SECTOR_BITS;
    int nb_sectors = req->bytes >> BDRV_SECTOR_BITS;

    assert(req->bs == blk_bs(job->common.blk));
    assert((req->offset & (BDRV_SECTOR_SIZE - 1)) == 0);
    assert((req->bytes & (BDRV_SECTOR_SIZE - 1)) == 0);

    return backup_do_cow(job, sector_num, nb_sectors, NULL, true);
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
    BlockDriverState *bs = blk_bs(job->common.blk);

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

static void backup_clean(BlockJob *job)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common);
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

    assert(job->driver->job_type == BLOCK_JOB_TYPE_BACKUP);

    if (backup_job->sync_mode != MIRROR_SYNC_MODE_NONE) {
        error_setg(errp, "The backup job only supports block checkpoint in"
                   " sync=none mode");
        return;
    }

    len = DIV_ROUND_UP(backup_job->common.len, backup_job->cluster_size);
    bitmap_zero(backup_job->done_bitmap, len);
}

void backup_wait_for_overlapping_requests(BlockJob *job, int64_t sector_num,
                                          int nb_sectors)
{
    BackupBlockJob *backup_job = container_of(job, BackupBlockJob, common);
    int64_t sectors_per_cluster = cluster_size_sectors(backup_job);
    int64_t start, end;

    assert(job->driver->job_type == BLOCK_JOB_TYPE_BACKUP);

    start = sector_num / sectors_per_cluster;
    end = DIV_ROUND_UP(sector_num + nb_sectors, sectors_per_cluster);
    wait_for_overlapping_requests(backup_job, start, end);
}

void backup_cow_request_begin(CowRequest *req, BlockJob *job,
                              int64_t sector_num,
                              int nb_sectors)
{
    BackupBlockJob *backup_job = container_of(job, BackupBlockJob, common);
    int64_t sectors_per_cluster = cluster_size_sectors(backup_job);
    int64_t start, end;

    assert(job->driver->job_type == BLOCK_JOB_TYPE_BACKUP);

    start = sector_num / sectors_per_cluster;
    end = DIV_ROUND_UP(sector_num + nb_sectors, sectors_per_cluster);
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

typedef struct {
    int ret;
} BackupCompleteData;

static void backup_complete(BlockJob *job, void *opaque)
{
    BackupCompleteData *data = opaque;

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
    BdrvDirtyBitmapIter *dbi;

    granularity = bdrv_dirty_bitmap_granularity(job->sync_bitmap);
    clusters_per_iter = MAX((granularity / job->cluster_size), 1);
    dbi = bdrv_dirty_iter_new(job->sync_bitmap, 0);

    /* Find the next dirty sector(s) */
    while ((sector = bdrv_dirty_iter_next(dbi)) != -1) {
        cluster = sector / sectors_per_cluster;

        /* Fake progress updates for any clusters we skipped */
        if (cluster != last_cluster + 1) {
            job->common.offset += ((cluster - last_cluster - 1) *
                                   job->cluster_size);
        }

        for (end = cluster + clusters_per_iter; cluster < end; cluster++) {
            do {
                if (yield_and_check(job)) {
                    goto out;
                }
                ret = backup_do_cow(job, cluster * sectors_per_cluster,
                                    sectors_per_cluster, &error_is_read,
                                    false);
                if ((ret < 0) &&
                    backup_error_action(job, error_is_read, -ret) ==
                    BLOCK_ERROR_ACTION_REPORT) {
                    goto out;
                }
            } while (ret < 0);
        }

        /* If the bitmap granularity is smaller than the backup granularity,
         * we need to advance the iterator pointer to the next cluster. */
        if (granularity < job->cluster_size) {
            bdrv_set_dirty_iter(dbi, cluster * sectors_per_cluster);
        }

        last_cluster = cluster - 1;
    }

    /* Play some final catchup with the progress meter */
    end = DIV_ROUND_UP(job->common.len, job->cluster_size);
    if (last_cluster + 1 < end) {
        job->common.offset += ((end - last_cluster - 1) * job->cluster_size);
    }

out:
    bdrv_dirty_iter_free(dbi);
    return ret;
}

static void coroutine_fn backup_run(void *opaque)
{
    BackupBlockJob *job = opaque;
    BackupCompleteData *data;
    BlockDriverState *bs = blk_bs(job->common.blk);
    int64_t start, end;
    int64_t sectors_per_cluster = cluster_size_sectors(job);
    int ret = 0;

    QLIST_INIT(&job->inflight_reqs);
    qemu_co_rwlock_init(&job->flush_rwlock);

    start = 0;
    end = DIV_ROUND_UP(job->common.len, job->cluster_size);

    job->done_bitmap = bitmap_new(end);

    job->before_write.notify = backup_before_write_notify;
    bdrv_add_before_write_notifier(bs, &job->before_write);

    if (job->sync_mode == MIRROR_SYNC_MODE_NONE) {
        while (!block_job_is_cancelled(&job->common)) {
            /* Yield until the job is cancelled.  We just let our before_write
             * notify callback service CoW requests. */
            block_job_yield(&job->common);
        }
    } else if (job->sync_mode == MIRROR_SYNC_MODE_INCREMENTAL) {
        ret = backup_run_incremental(job);
    } else {
        /* Both FULL and TOP SYNC_MODE's require copying.. */
        for (; start < end; start++) {
            bool error_is_read;
            int alloced = 0;

            if (yield_and_check(job)) {
                break;
            }

            if (job->sync_mode == MIRROR_SYNC_MODE_TOP) {
                int i, n;

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
                ret = backup_do_cow(job, start * sectors_per_cluster,
                                    sectors_per_cluster, &error_is_read,
                                    false);
            }
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

    notifier_with_return_remove(&job->before_write);

    /* wait until pending backup_do_cow() calls have completed */
    qemu_co_rwlock_wrlock(&job->flush_rwlock);
    qemu_co_rwlock_unlock(&job->flush_rwlock);
    g_free(job->done_bitmap);

    data = g_malloc(sizeof(*data));
    data->ret = ret;
    block_job_defer_to_main_loop(&job->common, backup_complete, data);
}

static const BlockJobDriver backup_job_driver = {
    .instance_size          = sizeof(BackupBlockJob),
    .job_type               = BLOCK_JOB_TYPE_BACKUP,
    .start                  = backup_run,
    .set_speed              = backup_set_speed,
    .commit                 = backup_commit,
    .abort                  = backup_abort,
    .clean                  = backup_clean,
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
                  BlockJobTxn *txn, Error **errp)
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
                   MirrorSyncMode_lookup[sync_mode]);
        return NULL;
    }

    len = bdrv_getlength(bs);
    if (len < 0) {
        error_setg_errno(errp, -len, "unable to get length for '%s'",
                         bdrv_get_device_name(bs));
        goto error;
    }

    /* job->common.len is fixed, so we can't allow resize */
    job = block_job_create(job_id, &backup_job_driver, bs,
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

    /* If there is no backing file on the target, we cannot rely on COW if our
     * backup cluster size is smaller than the target cluster size. Even for
     * targets with a backing file, try to avoid COW if possible. */
    ret = bdrv_get_info(target, &bdi);
    if (ret == -ENOTSUP && !target->backing) {
        /* Cluster size is not defined */
        error_report("WARNING: The target block device doesn't provide "
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

    /* Required permissions are already taken with target's blk_new() */
    block_job_add_bdrv(&job->common, "target", target, 0, BLK_PERM_ALL,
                       &error_abort);
    job->common.len = len;
    block_job_txn_add_job(txn, &job->common);

    return &job->common;

 error:
    if (sync_bitmap) {
        bdrv_reclaim_dirty_bitmap(bs, sync_bitmap, NULL);
    }
    if (job) {
        backup_clean(&job->common);
        block_job_unref(&job->common);
    }

    return NULL;
}
