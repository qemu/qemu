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

#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#include "trace.h"
#include "block/block.h"
#include "block/block_int.h"
#include "block/blockjob.h"
#include "qemu/ratelimit.h"

#define BACKUP_CLUSTER_BITS 16
#define BACKUP_CLUSTER_SIZE (1 << BACKUP_CLUSTER_BITS)
#define BACKUP_SECTORS_PER_CLUSTER (BACKUP_CLUSTER_SIZE / BDRV_SECTOR_SIZE)

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
    MirrorSyncMode sync_mode;
    RateLimit limit;
    BlockdevOnError on_source_error;
    BlockdevOnError on_target_error;
    CoRwlock flush_rwlock;
    uint64_t sectors_read;
    HBitmap *bitmap;
    QLIST_HEAD(, CowRequest) inflight_reqs;
} BackupBlockJob;

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
                                      bool *error_is_read)
{
    BackupBlockJob *job = (BackupBlockJob *)bs->job;
    CowRequest cow_request;
    struct iovec iov;
    QEMUIOVector bounce_qiov;
    void *bounce_buffer = NULL;
    int ret = 0;
    int64_t start, end;
    int n;

    qemu_co_rwlock_rdlock(&job->flush_rwlock);

    start = sector_num / BACKUP_SECTORS_PER_CLUSTER;
    end = DIV_ROUND_UP(sector_num + nb_sectors, BACKUP_SECTORS_PER_CLUSTER);

    trace_backup_do_cow_enter(job, start, sector_num, nb_sectors);

    wait_for_overlapping_requests(job, start, end);
    cow_request_begin(&cow_request, job, start, end);

    for (; start < end; start++) {
        if (hbitmap_get(job->bitmap, start)) {
            trace_backup_do_cow_skip(job, start);
            continue; /* already copied */
        }

        trace_backup_do_cow_process(job, start);

        n = MIN(BACKUP_SECTORS_PER_CLUSTER,
                job->common.len / BDRV_SECTOR_SIZE -
                start * BACKUP_SECTORS_PER_CLUSTER);

        if (!bounce_buffer) {
            bounce_buffer = qemu_blockalign(bs, BACKUP_CLUSTER_SIZE);
        }
        iov.iov_base = bounce_buffer;
        iov.iov_len = n * BDRV_SECTOR_SIZE;
        qemu_iovec_init_external(&bounce_qiov, &iov, 1);

        ret = bdrv_co_readv(bs, start * BACKUP_SECTORS_PER_CLUSTER, n,
                            &bounce_qiov);
        if (ret < 0) {
            trace_backup_do_cow_read_fail(job, start, ret);
            if (error_is_read) {
                *error_is_read = true;
            }
            goto out;
        }

        if (buffer_is_zero(iov.iov_base, iov.iov_len)) {
            ret = bdrv_co_write_zeroes(job->target,
                                       start * BACKUP_SECTORS_PER_CLUSTER,
                                       n, BDRV_REQ_MAY_UNMAP);
        } else {
            ret = bdrv_co_writev(job->target,
                                 start * BACKUP_SECTORS_PER_CLUSTER, n,
                                 &bounce_qiov);
        }
        if (ret < 0) {
            trace_backup_do_cow_write_fail(job, start, ret);
            if (error_is_read) {
                *error_is_read = false;
            }
            goto out;
        }

        hbitmap_set(job->bitmap, start, 1);

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

    return backup_do_cow(req->bs, sector_num, nb_sectors, NULL);
}

static void backup_set_speed(BlockJob *job, int64_t speed, Error **errp)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common);

    if (speed < 0) {
        error_set(errp, QERR_INVALID_PARAMETER, "speed");
        return;
    }
    ratelimit_set_speed(&s->limit, speed / BDRV_SECTOR_SIZE, SLICE_TIME);
}

static void backup_iostatus_reset(BlockJob *job)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common);

    bdrv_iostatus_reset(s->target);
}

static const BlockJobDriver backup_job_driver = {
    .instance_size  = sizeof(BackupBlockJob),
    .job_type       = BLOCK_JOB_TYPE_BACKUP,
    .set_speed      = backup_set_speed,
    .iostatus_reset = backup_iostatus_reset,
};

static BlockErrorAction backup_error_action(BackupBlockJob *job,
                                            bool read, int error)
{
    if (read) {
        return block_job_error_action(&job->common, job->common.bs,
                                      job->on_source_error, true, error);
    } else {
        return block_job_error_action(&job->common, job->target,
                                      job->on_target_error, false, error);
    }
}

static void coroutine_fn backup_run(void *opaque)
{
    BackupBlockJob *job = opaque;
    BlockDriverState *bs = job->common.bs;
    BlockDriverState *target = job->target;
    BlockdevOnError on_target_error = job->on_target_error;
    NotifierWithReturn before_write = {
        .notify = backup_before_write_notify,
    };
    int64_t start, end;
    int ret = 0;

    QLIST_INIT(&job->inflight_reqs);
    qemu_co_rwlock_init(&job->flush_rwlock);

    start = 0;
    end = DIV_ROUND_UP(job->common.len / BDRV_SECTOR_SIZE,
                       BACKUP_SECTORS_PER_CLUSTER);

    job->bitmap = hbitmap_alloc(end, 0);

    bdrv_set_enable_write_cache(target, true);
    bdrv_set_on_error(target, on_target_error, on_target_error);
    bdrv_iostatus_enable(target);

    bdrv_add_before_write_notifier(bs, &before_write);

    if (job->sync_mode == MIRROR_SYNC_MODE_NONE) {
        while (!block_job_is_cancelled(&job->common)) {
            /* Yield until the job is cancelled.  We just let our before_write
             * notify callback service CoW requests. */
            job->common.busy = false;
            qemu_coroutine_yield();
            job->common.busy = true;
        }
    } else {
        /* Both FULL and TOP SYNC_MODE's require copying.. */
        for (; start < end; start++) {
            bool error_is_read;

            if (block_job_is_cancelled(&job->common)) {
                break;
            }

            /* we need to yield so that qemu_aio_flush() returns.
             * (without, VM does not reboot)
             */
            if (job->common.speed) {
                uint64_t delay_ns = ratelimit_calculate_delay(
                        &job->limit, job->sectors_read);
                job->sectors_read = 0;
                block_job_sleep_ns(&job->common, QEMU_CLOCK_REALTIME, delay_ns);
            } else {
                block_job_sleep_ns(&job->common, QEMU_CLOCK_REALTIME, 0);
            }

            if (block_job_is_cancelled(&job->common)) {
                break;
            }

            if (job->sync_mode == MIRROR_SYNC_MODE_TOP) {
                int i, n;
                int alloced = 0;

                /* Check to see if these blocks are already in the
                 * backing file. */

                for (i = 0; i < BACKUP_SECTORS_PER_CLUSTER;) {
                    /* bdrv_is_allocated() only returns true/false based
                     * on the first set of sectors it comes across that
                     * are are all in the same state.
                     * For that reason we must verify each sector in the
                     * backup cluster length.  We end up copying more than
                     * needed but at some point that is always the case. */
                    alloced =
                        bdrv_is_allocated(bs,
                                start * BACKUP_SECTORS_PER_CLUSTER + i,
                                BACKUP_SECTORS_PER_CLUSTER - i, &n);
                    i += n;

                    if (alloced == 1) {
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
            ret = backup_do_cow(bs, start * BACKUP_SECTORS_PER_CLUSTER,
                    BACKUP_SECTORS_PER_CLUSTER, &error_is_read);
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

    hbitmap_free(job->bitmap);

    bdrv_iostatus_disable(target);
    bdrv_unref(target);

    block_job_completed(&job->common, ret);
}

void backup_start(BlockDriverState *bs, BlockDriverState *target,
                  int64_t speed, MirrorSyncMode sync_mode,
                  BlockdevOnError on_source_error,
                  BlockdevOnError on_target_error,
                  BlockDriverCompletionFunc *cb, void *opaque,
                  Error **errp)
{
    int64_t len;

    assert(bs);
    assert(target);
    assert(cb);

    if ((on_source_error == BLOCKDEV_ON_ERROR_STOP ||
         on_source_error == BLOCKDEV_ON_ERROR_ENOSPC) &&
        !bdrv_iostatus_is_enabled(bs)) {
        error_set(errp, QERR_INVALID_PARAMETER, "on-source-error");
        return;
    }

    len = bdrv_getlength(bs);
    if (len < 0) {
        error_setg_errno(errp, -len, "unable to get length for '%s'",
                         bdrv_get_device_name(bs));
        return;
    }

    BackupBlockJob *job = block_job_create(&backup_job_driver, bs, speed,
                                           cb, opaque, errp);
    if (!job) {
        return;
    }

    job->on_source_error = on_source_error;
    job->on_target_error = on_target_error;
    job->target = target;
    job->sync_mode = sync_mode;
    job->common.len = len;
    job->common.co = qemu_coroutine_create(backup_run);
    qemu_coroutine_enter(job->common.co, job);
}
