/*
 * QEMU backup
 *
 * Copyright (C) 2013 Proxmox Server Solutions
 * Copyright (c) 2019 Virtuozzo International GmbH.
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
#include "block/block-copy.h"
#include "block/dirty-bitmap.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "sysemu/block-backend.h"
#include "qemu/bitmap.h"
#include "qemu/error-report.h"

#include "block/copy-before-write.h"

typedef struct BackupBlockJob {
    BlockJob common;
    BlockDriverState *cbw;
    BlockDriverState *source_bs;
    BlockDriverState *target_bs;

    BdrvDirtyBitmap *sync_bitmap;

    MirrorSyncMode sync_mode;
    BitmapSyncMode bitmap_mode;
    BlockdevOnError on_source_error;
    BlockdevOnError on_target_error;
    uint64_t len;
    int64_t cluster_size;
    BackupPerf perf;

    BlockCopyState *bcs;

    bool wait;
    BlockCopyCallState *bg_bcs_call;
} BackupBlockJob;

static const BlockJobDriver backup_job_driver;

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
        bm = bdrv_dirty_bitmap_abdicate(job->sync_bitmap, NULL);
    } else {
        /*
         * We failed, or we never intended to sync the bitmap anyway.
         * Merge the successor back into the parent, keeping all data.
         */
        bm = bdrv_reclaim_dirty_bitmap(job->sync_bitmap, NULL);
    }

    assert(bm);

    if (ret < 0 && job->bitmap_mode == BITMAP_SYNC_MODE_ALWAYS) {
        /* If we failed and synced, merge in the bits we didn't copy: */
        bdrv_dirty_bitmap_merge_internal(bm, block_copy_dirty_bitmap(job->bcs),
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
    block_job_remove_all_bdrv(&s->common);
    bdrv_cbw_drop(s->cbw);
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

    bdrv_set_dirty_bitmap(block_copy_dirty_bitmap(backup_job->bcs), 0,
                          backup_job->len);
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

static void coroutine_fn backup_block_copy_callback(void *opaque)
{
    BackupBlockJob *s = opaque;

    if (s->wait) {
        s->wait = false;
        aio_co_wake(s->common.job.co);
    } else {
        job_enter(&s->common.job);
    }
}

static int coroutine_fn backup_loop(BackupBlockJob *job)
{
    BlockCopyCallState *s = NULL;
    int ret = 0;
    bool error_is_read;
    BlockErrorAction act;

    while (true) { /* retry loop */
        job->bg_bcs_call = s = block_copy_async(job->bcs, 0,
                QEMU_ALIGN_UP(job->len, job->cluster_size),
                job->perf.max_workers, job->perf.max_chunk,
                backup_block_copy_callback, job);

        while (!block_copy_call_finished(s) &&
               !job_is_cancelled(&job->common.job))
        {
            job_yield(&job->common.job);
        }

        if (!block_copy_call_finished(s)) {
            assert(job_is_cancelled(&job->common.job));
            /*
             * Note that we can't use job_yield() here, as it doesn't work for
             * cancelled job.
             */
            block_copy_call_cancel(s);
            job->wait = true;
            qemu_coroutine_yield();
            assert(block_copy_call_finished(s));
            ret = 0;
            goto out;
        }

        if (job_is_cancelled(&job->common.job) ||
            block_copy_call_succeeded(s))
        {
            ret = 0;
            goto out;
        }

        if (block_copy_call_cancelled(s)) {
            /*
             * Job is not cancelled but only block-copy call. This is possible
             * after job pause. Now the pause is finished, start new block-copy
             * iteration.
             */
            block_copy_call_free(s);
            continue;
        }

        /* The only remaining case is failed block-copy call. */
        assert(block_copy_call_failed(s));

        ret = block_copy_call_status(s, &error_is_read);
        act = backup_error_action(job, error_is_read, -ret);
        switch (act) {
        case BLOCK_ERROR_ACTION_REPORT:
            goto out;
        case BLOCK_ERROR_ACTION_STOP:
            /*
             * Go to pause prior to starting new block-copy call on the next
             * iteration.
             */
            job_pause_point(&job->common.job);
            break;
        case BLOCK_ERROR_ACTION_IGNORE:
            /* Proceed to new block-copy call to retry. */
            break;
        default:
            abort();
        }

        block_copy_call_free(s);
    }

out:
    block_copy_call_free(s);
    job->bg_bcs_call = NULL;
    return ret;
}

static void backup_init_bcs_bitmap(BackupBlockJob *job)
{
    uint64_t estimate;
    BdrvDirtyBitmap *bcs_bitmap = block_copy_dirty_bitmap(job->bcs);

    if (job->sync_mode == MIRROR_SYNC_MODE_BITMAP) {
        bdrv_clear_dirty_bitmap(bcs_bitmap, NULL);
        bdrv_dirty_bitmap_merge_internal(bcs_bitmap, job->sync_bitmap, NULL,
                                         true);
    } else if (job->sync_mode == MIRROR_SYNC_MODE_TOP) {
        /*
         * We can't hog the coroutine to initialize this thoroughly.
         * Set a flag and resume work when we are able to yield safely.
         */
        block_copy_set_skip_unallocated(job->bcs, true);
    }

    estimate = bdrv_get_dirty_count(bcs_bitmap);
    job_progress_set_remaining(&job->common.job, estimate);
}

static int coroutine_fn backup_run(Job *job, Error **errp)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common.job);
    int ret;

    backup_init_bcs_bitmap(s);

    if (s->sync_mode == MIRROR_SYNC_MODE_TOP) {
        int64_t offset = 0;
        int64_t count;

        for (offset = 0; offset < s->len; ) {
            if (job_is_cancelled(job)) {
                return -ECANCELED;
            }

            job_pause_point(job);

            if (job_is_cancelled(job)) {
                return -ECANCELED;
            }

            /* rdlock protects the subsequent call to bdrv_is_allocated() */
            bdrv_graph_co_rdlock();
            ret = block_copy_reset_unallocated(s->bcs, offset, &count);
            bdrv_graph_co_rdunlock();
            if (ret < 0) {
                return ret;
            }

            offset += count;
        }
        block_copy_set_skip_unallocated(s->bcs, false);
    }

    if (s->sync_mode == MIRROR_SYNC_MODE_NONE) {
        /*
         * All bits are set in bcs bitmap to allow any cluster to be copied.
         * This does not actually require them to be copied.
         */
        while (!job_is_cancelled(job)) {
            /*
             * Yield until the job is cancelled.  We just let our before_write
             * notify callback service CoW requests.
             */
            job_yield(job);
        }
    } else {
        return backup_loop(s);
    }

    return 0;
}

static void coroutine_fn backup_pause(Job *job)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common.job);

    if (s->bg_bcs_call && !block_copy_call_finished(s->bg_bcs_call)) {
        block_copy_call_cancel(s->bg_bcs_call);
        s->wait = true;
        qemu_coroutine_yield();
    }
}

static void backup_set_speed(BlockJob *job, int64_t speed)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common);

    /*
     * block_job_set_speed() is called first from block_job_create(), when we
     * don't yet have s->bcs.
     */
    if (s->bcs) {
        block_copy_set_speed(s->bcs, speed);
        if (s->bg_bcs_call) {
            block_copy_kick(s->bg_bcs_call);
        }
    }
}

static bool backup_cancel(Job *job, bool force)
{
    BackupBlockJob *s = container_of(job, BackupBlockJob, common.job);

    bdrv_cancel_in_flight(s->target_bs);
    return true;
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
        .pause                  = backup_pause,
        .cancel                 = backup_cancel,
    },
    .set_speed = backup_set_speed,
};

BlockJob *backup_job_create(const char *job_id, BlockDriverState *bs,
                  BlockDriverState *target, int64_t speed,
                  MirrorSyncMode sync_mode, BdrvDirtyBitmap *sync_bitmap,
                  BitmapSyncMode bitmap_mode,
                  bool compress,
                  const char *filter_node_name,
                  BackupPerf *perf,
                  BlockdevOnError on_source_error,
                  BlockdevOnError on_target_error,
                  int creation_flags,
                  BlockCompletionFunc *cb, void *opaque,
                  JobTxn *txn, Error **errp)
{
    int64_t len, target_len;
    BackupBlockJob *job = NULL;
    int64_t cluster_size;
    BlockDriverState *cbw = NULL;
    BlockCopyState *bcs = NULL;

    assert(bs);
    assert(target);
    GLOBAL_STATE_CODE();

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

    if (compress && !bdrv_supports_compressed_writes(target)) {
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

    if (perf->max_workers < 1 || perf->max_workers > INT_MAX) {
        error_setg(errp, "max-workers must be between 1 and %d", INT_MAX);
        return NULL;
    }

    if (perf->max_chunk < 0) {
        error_setg(errp, "max-chunk must be zero (which means no limit) or "
                   "positive");
        return NULL;
    }

    if (sync_bitmap) {
        /* If we need to write to this bitmap, check that we can: */
        if (bitmap_mode != BITMAP_SYNC_MODE_NEVER &&
            bdrv_dirty_bitmap_check(sync_bitmap, BDRV_BITMAP_DEFAULT, errp)) {
            return NULL;
        }

        /* Create a new bitmap, and freeze/disable this one. */
        if (bdrv_dirty_bitmap_create_successor(sync_bitmap, errp) < 0) {
            return NULL;
        }
    }

    len = bdrv_getlength(bs);
    if (len < 0) {
        error_setg_errno(errp, -len, "Unable to get length for '%s'",
                         bdrv_get_device_or_node_name(bs));
        goto error;
    }

    target_len = bdrv_getlength(target);
    if (target_len < 0) {
        error_setg_errno(errp, -target_len, "Unable to get length for '%s'",
                         bdrv_get_device_or_node_name(bs));
        goto error;
    }

    if (target_len != len) {
        error_setg(errp, "Source and target image have different sizes");
        goto error;
    }

    cbw = bdrv_cbw_append(bs, target, filter_node_name, &bcs, errp);
    if (!cbw) {
        goto error;
    }

    cluster_size = block_copy_cluster_size(bcs);

    if (perf->max_chunk && perf->max_chunk < cluster_size) {
        error_setg(errp, "Required max-chunk (%" PRIi64 ") is less than backup "
                   "cluster size (%" PRIi64 ")", perf->max_chunk, cluster_size);
        goto error;
    }

    /* job->len is fixed, so we can't allow resize */
    job = block_job_create(job_id, &backup_job_driver, txn, cbw,
                           0, BLK_PERM_ALL,
                           speed, creation_flags, cb, opaque, errp);
    if (!job) {
        goto error;
    }

    job->cbw = cbw;
    job->source_bs = bs;
    job->target_bs = target;
    job->on_source_error = on_source_error;
    job->on_target_error = on_target_error;
    job->sync_mode = sync_mode;
    job->sync_bitmap = sync_bitmap;
    job->bitmap_mode = bitmap_mode;
    job->bcs = bcs;
    job->cluster_size = cluster_size;
    job->len = len;
    job->perf = *perf;

    block_copy_set_copy_opts(bcs, perf->use_copy_range, compress);
    block_copy_set_progress_meter(bcs, &job->common.job.progress);
    block_copy_set_speed(bcs, speed);

    /* Required permissions are taken by copy-before-write filter target */
    block_job_add_bdrv(&job->common, "target", target, 0, BLK_PERM_ALL,
                       &error_abort);

    return &job->common;

 error:
    if (sync_bitmap) {
        bdrv_reclaim_dirty_bitmap(sync_bitmap, NULL);
    }
    if (cbw) {
        bdrv_cbw_drop(cbw);
    }

    return NULL;
}
