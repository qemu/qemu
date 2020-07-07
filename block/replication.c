/*
 * Replication Block filter
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2016 FUJITSU LIMITED
 *
 * Author:
 *   Wen Congyang <wency@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "block/nbd.h"
#include "block/blockjob.h"
#include "block/block_int.h"
#include "block/block_backup.h"
#include "sysemu/block-backend.h"
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "replication.h"

typedef enum {
    BLOCK_REPLICATION_NONE,             /* block replication is not started */
    BLOCK_REPLICATION_RUNNING,          /* block replication is running */
    BLOCK_REPLICATION_FAILOVER,         /* failover is running in background */
    BLOCK_REPLICATION_FAILOVER_FAILED,  /* failover failed */
    BLOCK_REPLICATION_DONE,             /* block replication is done */
} ReplicationStage;

typedef struct BDRVReplicationState {
    ReplicationMode mode;
    ReplicationStage stage;
    BdrvChild *active_disk;
    BlockJob *commit_job;
    BdrvChild *hidden_disk;
    BdrvChild *secondary_disk;
    BlockJob *backup_job;
    char *top_id;
    ReplicationState *rs;
    Error *blocker;
    bool orig_hidden_read_only;
    bool orig_secondary_read_only;
    int error;
} BDRVReplicationState;

static void replication_start(ReplicationState *rs, ReplicationMode mode,
                              Error **errp);
static void replication_do_checkpoint(ReplicationState *rs, Error **errp);
static void replication_get_error(ReplicationState *rs, Error **errp);
static void replication_stop(ReplicationState *rs, bool failover,
                             Error **errp);

#define REPLICATION_MODE        "mode"
#define REPLICATION_TOP_ID      "top-id"
static QemuOptsList replication_runtime_opts = {
    .name = "replication",
    .head = QTAILQ_HEAD_INITIALIZER(replication_runtime_opts.head),
    .desc = {
        {
            .name = REPLICATION_MODE,
            .type = QEMU_OPT_STRING,
        },
        {
            .name = REPLICATION_TOP_ID,
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

static ReplicationOps replication_ops = {
    .start = replication_start,
    .checkpoint = replication_do_checkpoint,
    .get_error = replication_get_error,
    .stop = replication_stop,
};

static int replication_open(BlockDriverState *bs, QDict *options,
                            int flags, Error **errp)
{
    int ret;
    BDRVReplicationState *s = bs->opaque;
    QemuOpts *opts = NULL;
    const char *mode;
    const char *top_id;

    bs->file = bdrv_open_child(NULL, options, "file", bs, &child_of_bds,
                               BDRV_CHILD_FILTERED | BDRV_CHILD_PRIMARY,
                               false, errp);
    if (!bs->file) {
        return -EINVAL;
    }

    ret = -EINVAL;
    opts = qemu_opts_create(&replication_runtime_opts, NULL, 0, &error_abort);
    if (!qemu_opts_absorb_qdict(opts, options, errp)) {
        goto fail;
    }

    mode = qemu_opt_get(opts, REPLICATION_MODE);
    if (!mode) {
        error_setg(errp, "Missing the option mode");
        goto fail;
    }

    if (!strcmp(mode, "primary")) {
        s->mode = REPLICATION_MODE_PRIMARY;
        top_id = qemu_opt_get(opts, REPLICATION_TOP_ID);
        if (top_id) {
            error_setg(errp,
                       "The primary side does not support option top-id");
            goto fail;
        }
    } else if (!strcmp(mode, "secondary")) {
        s->mode = REPLICATION_MODE_SECONDARY;
        top_id = qemu_opt_get(opts, REPLICATION_TOP_ID);
        s->top_id = g_strdup(top_id);
        if (!s->top_id) {
            error_setg(errp, "Missing the option top-id");
            goto fail;
        }
    } else {
        error_setg(errp,
                   "The option mode's value should be primary or secondary");
        goto fail;
    }

    s->rs = replication_new(bs, &replication_ops);

    ret = 0;

fail:
    qemu_opts_del(opts);
    return ret;
}

static void replication_close(BlockDriverState *bs)
{
    BDRVReplicationState *s = bs->opaque;
    Job *commit_job;

    if (s->stage == BLOCK_REPLICATION_RUNNING) {
        replication_stop(s->rs, false, NULL);
    }
    if (s->stage == BLOCK_REPLICATION_FAILOVER) {
        commit_job = &s->commit_job->job;
        assert(commit_job->aio_context == qemu_get_current_aio_context());
        job_cancel_sync(commit_job);
    }

    if (s->mode == REPLICATION_MODE_SECONDARY) {
        g_free(s->top_id);
    }

    replication_remove(s->rs);
}

static void replication_child_perm(BlockDriverState *bs, BdrvChild *c,
                                   BdrvChildRole role,
                                   BlockReopenQueue *reopen_queue,
                                   uint64_t perm, uint64_t shared,
                                   uint64_t *nperm, uint64_t *nshared)
{
    *nperm = BLK_PERM_CONSISTENT_READ;
    if ((bs->open_flags & (BDRV_O_INACTIVE | BDRV_O_RDWR)) == BDRV_O_RDWR) {
        *nperm |= BLK_PERM_WRITE;
    }
    *nshared = BLK_PERM_CONSISTENT_READ
               | BLK_PERM_WRITE
               | BLK_PERM_WRITE_UNCHANGED;
    return;
}

static int64_t replication_getlength(BlockDriverState *bs)
{
    return bdrv_getlength(bs->file->bs);
}

static int replication_get_io_status(BDRVReplicationState *s)
{
    switch (s->stage) {
    case BLOCK_REPLICATION_NONE:
        return -EIO;
    case BLOCK_REPLICATION_RUNNING:
        return 0;
    case BLOCK_REPLICATION_FAILOVER:
        return s->mode == REPLICATION_MODE_PRIMARY ? -EIO : 0;
    case BLOCK_REPLICATION_FAILOVER_FAILED:
        return s->mode == REPLICATION_MODE_PRIMARY ? -EIO : 1;
    case BLOCK_REPLICATION_DONE:
        /*
         * active commit job completes, and active disk and secondary_disk
         * is swapped, so we can operate bs->file directly
         */
        return s->mode == REPLICATION_MODE_PRIMARY ? -EIO : 0;
    default:
        abort();
    }
}

static int replication_return_value(BDRVReplicationState *s, int ret)
{
    if (s->mode == REPLICATION_MODE_SECONDARY) {
        return ret;
    }

    if (ret < 0) {
        s->error = ret;
        ret = 0;
    }

    return ret;
}

static coroutine_fn int replication_co_readv(BlockDriverState *bs,
                                             int64_t sector_num,
                                             int remaining_sectors,
                                             QEMUIOVector *qiov)
{
    BDRVReplicationState *s = bs->opaque;
    int ret;

    if (s->mode == REPLICATION_MODE_PRIMARY) {
        /* We only use it to forward primary write requests */
        return -EIO;
    }

    ret = replication_get_io_status(s);
    if (ret < 0) {
        return ret;
    }

    ret = bdrv_co_preadv(bs->file, sector_num * BDRV_SECTOR_SIZE,
                         remaining_sectors * BDRV_SECTOR_SIZE, qiov, 0);

    return replication_return_value(s, ret);
}

static coroutine_fn int replication_co_writev(BlockDriverState *bs,
                                              int64_t sector_num,
                                              int remaining_sectors,
                                              QEMUIOVector *qiov,
                                              int flags)
{
    BDRVReplicationState *s = bs->opaque;
    QEMUIOVector hd_qiov;
    uint64_t bytes_done = 0;
    BdrvChild *top = bs->file;
    BdrvChild *base = s->secondary_disk;
    BdrvChild *target;
    int ret;
    int64_t n;

    assert(!flags);
    ret = replication_get_io_status(s);
    if (ret < 0) {
        goto out;
    }

    if (ret == 0) {
        ret = bdrv_co_pwritev(top, sector_num * BDRV_SECTOR_SIZE,
                              remaining_sectors * BDRV_SECTOR_SIZE, qiov, 0);
        return replication_return_value(s, ret);
    }

    /*
     * Failover failed, only write to active disk if the sectors
     * have already been allocated in active disk/hidden disk.
     */
    qemu_iovec_init(&hd_qiov, qiov->niov);
    while (remaining_sectors > 0) {
        int64_t count;

        ret = bdrv_is_allocated_above(top->bs, base->bs, false,
                                      sector_num * BDRV_SECTOR_SIZE,
                                      remaining_sectors * BDRV_SECTOR_SIZE,
                                      &count);
        if (ret < 0) {
            goto out1;
        }

        assert(QEMU_IS_ALIGNED(count, BDRV_SECTOR_SIZE));
        n = count >> BDRV_SECTOR_BITS;
        qemu_iovec_reset(&hd_qiov);
        qemu_iovec_concat(&hd_qiov, qiov, bytes_done, count);

        target = ret ? top : base;
        ret = bdrv_co_pwritev(target, sector_num * BDRV_SECTOR_SIZE,
                              n * BDRV_SECTOR_SIZE, &hd_qiov, 0);
        if (ret < 0) {
            goto out1;
        }

        remaining_sectors -= n;
        sector_num += n;
        bytes_done += count;
    }

out1:
    qemu_iovec_destroy(&hd_qiov);
out:
    return ret;
}

static void secondary_do_checkpoint(BDRVReplicationState *s, Error **errp)
{
    Error *local_err = NULL;
    int ret;

    if (!s->backup_job) {
        error_setg(errp, "Backup job was cancelled unexpectedly");
        return;
    }

    backup_do_checkpoint(s->backup_job, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (!s->active_disk->bs->drv) {
        error_setg(errp, "Active disk %s is ejected",
                   s->active_disk->bs->node_name);
        return;
    }

    ret = bdrv_make_empty(s->active_disk, errp);
    if (ret < 0) {
        return;
    }

    if (!s->hidden_disk->bs->drv) {
        error_setg(errp, "Hidden disk %s is ejected",
                   s->hidden_disk->bs->node_name);
        return;
    }

    BlockBackend *blk = blk_new(qemu_get_current_aio_context(),
                                BLK_PERM_WRITE, BLK_PERM_ALL);
    blk_insert_bs(blk, s->hidden_disk->bs, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        blk_unref(blk);
        return;
    }

    ret = blk_make_empty(blk, errp);
    blk_unref(blk);
    if (ret < 0) {
        return;
    }
}

/* This function is supposed to be called twice:
 * first with writable = true, then with writable = false.
 * The first call puts s->hidden_disk and s->secondary_disk in
 * r/w mode, and the second puts them back in their original state.
 */
static void reopen_backing_file(BlockDriverState *bs, bool writable,
                                Error **errp)
{
    BDRVReplicationState *s = bs->opaque;
    BlockReopenQueue *reopen_queue = NULL;

    if (writable) {
        s->orig_hidden_read_only = bdrv_is_read_only(s->hidden_disk->bs);
        s->orig_secondary_read_only = bdrv_is_read_only(s->secondary_disk->bs);
    }

    bdrv_subtree_drained_begin(s->hidden_disk->bs);
    bdrv_subtree_drained_begin(s->secondary_disk->bs);

    if (s->orig_hidden_read_only) {
        QDict *opts = qdict_new();
        qdict_put_bool(opts, BDRV_OPT_READ_ONLY, !writable);
        reopen_queue = bdrv_reopen_queue(reopen_queue, s->hidden_disk->bs,
                                         opts, true);
    }

    if (s->orig_secondary_read_only) {
        QDict *opts = qdict_new();
        qdict_put_bool(opts, BDRV_OPT_READ_ONLY, !writable);
        reopen_queue = bdrv_reopen_queue(reopen_queue, s->secondary_disk->bs,
                                         opts, true);
    }

    if (reopen_queue) {
        bdrv_reopen_multiple(reopen_queue, errp);
    }

    bdrv_subtree_drained_end(s->hidden_disk->bs);
    bdrv_subtree_drained_end(s->secondary_disk->bs);
}

static void backup_job_cleanup(BlockDriverState *bs)
{
    BDRVReplicationState *s = bs->opaque;
    BlockDriverState *top_bs;

    s->backup_job = NULL;

    top_bs = bdrv_lookup_bs(s->top_id, s->top_id, NULL);
    if (!top_bs) {
        return;
    }
    bdrv_op_unblock_all(top_bs, s->blocker);
    error_free(s->blocker);
    reopen_backing_file(bs, false, NULL);
}

static void backup_job_completed(void *opaque, int ret)
{
    BlockDriverState *bs = opaque;
    BDRVReplicationState *s = bs->opaque;

    if (s->stage != BLOCK_REPLICATION_FAILOVER) {
        /* The backup job is cancelled unexpectedly */
        s->error = -EIO;
    }

    backup_job_cleanup(bs);
}

static bool check_top_bs(BlockDriverState *top_bs, BlockDriverState *bs)
{
    BdrvChild *child;

    /* The bs itself is the top_bs */
    if (top_bs == bs) {
        return true;
    }

    /* Iterate over top_bs's children */
    QLIST_FOREACH(child, &top_bs->children, next) {
        if (child->bs == bs || check_top_bs(child->bs, bs)) {
            return true;
        }
    }

    return false;
}

static void replication_start(ReplicationState *rs, ReplicationMode mode,
                              Error **errp)
{
    BlockDriverState *bs = rs->opaque;
    BDRVReplicationState *s;
    BlockDriverState *top_bs;
    int64_t active_length, hidden_length, disk_length;
    AioContext *aio_context;
    Error *local_err = NULL;

    aio_context = bdrv_get_aio_context(bs);
    aio_context_acquire(aio_context);
    s = bs->opaque;

    if (s->stage == BLOCK_REPLICATION_DONE ||
        s->stage == BLOCK_REPLICATION_FAILOVER) {
        /*
         * This case happens when a secondary is promoted to primary.
         * Ignore the request because the secondary side of replication
         * doesn't have to do anything anymore.
         */
        aio_context_release(aio_context);
        return;
    }

    if (s->stage != BLOCK_REPLICATION_NONE) {
        error_setg(errp, "Block replication is running or done");
        aio_context_release(aio_context);
        return;
    }

    if (s->mode != mode) {
        error_setg(errp, "The parameter mode's value is invalid, needs %d,"
                   " but got %d", s->mode, mode);
        aio_context_release(aio_context);
        return;
    }

    switch (s->mode) {
    case REPLICATION_MODE_PRIMARY:
        break;
    case REPLICATION_MODE_SECONDARY:
        s->active_disk = bs->file;
        if (!s->active_disk || !s->active_disk->bs ||
                                    !s->active_disk->bs->backing) {
            error_setg(errp, "Active disk doesn't have backing file");
            aio_context_release(aio_context);
            return;
        }

        s->hidden_disk = s->active_disk->bs->backing;
        if (!s->hidden_disk->bs || !s->hidden_disk->bs->backing) {
            error_setg(errp, "Hidden disk doesn't have backing file");
            aio_context_release(aio_context);
            return;
        }

        s->secondary_disk = s->hidden_disk->bs->backing;
        if (!s->secondary_disk->bs || !bdrv_has_blk(s->secondary_disk->bs)) {
            error_setg(errp, "The secondary disk doesn't have block backend");
            aio_context_release(aio_context);
            return;
        }

        /* verify the length */
        active_length = bdrv_getlength(s->active_disk->bs);
        hidden_length = bdrv_getlength(s->hidden_disk->bs);
        disk_length = bdrv_getlength(s->secondary_disk->bs);
        if (active_length < 0 || hidden_length < 0 || disk_length < 0 ||
            active_length != hidden_length || hidden_length != disk_length) {
            error_setg(errp, "Active disk, hidden disk, secondary disk's length"
                       " are not the same");
            aio_context_release(aio_context);
            return;
        }

        /* Must be true, or the bdrv_getlength() calls would have failed */
        assert(s->active_disk->bs->drv && s->hidden_disk->bs->drv);

        if (!s->active_disk->bs->drv->bdrv_make_empty ||
            !s->hidden_disk->bs->drv->bdrv_make_empty) {
            error_setg(errp,
                       "Active disk or hidden disk doesn't support make_empty");
            aio_context_release(aio_context);
            return;
        }

        /* reopen the backing file in r/w mode */
        reopen_backing_file(bs, true, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            aio_context_release(aio_context);
            return;
        }

        /* start backup job now */
        error_setg(&s->blocker,
                   "Block device is in use by internal backup job");

        top_bs = bdrv_lookup_bs(s->top_id, s->top_id, NULL);
        if (!top_bs || !bdrv_is_root_node(top_bs) ||
            !check_top_bs(top_bs, bs)) {
            error_setg(errp, "No top_bs or it is invalid");
            reopen_backing_file(bs, false, NULL);
            aio_context_release(aio_context);
            return;
        }
        bdrv_op_block_all(top_bs, s->blocker);
        bdrv_op_unblock(top_bs, BLOCK_OP_TYPE_DATAPLANE, s->blocker);

        s->backup_job = backup_job_create(
                                NULL, s->secondary_disk->bs, s->hidden_disk->bs,
                                0, MIRROR_SYNC_MODE_NONE, NULL, 0, false, NULL,
                                BLOCKDEV_ON_ERROR_REPORT,
                                BLOCKDEV_ON_ERROR_REPORT, JOB_INTERNAL,
                                backup_job_completed, bs, NULL, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            backup_job_cleanup(bs);
            aio_context_release(aio_context);
            return;
        }
        job_start(&s->backup_job->job);
        break;
    default:
        aio_context_release(aio_context);
        abort();
    }

    s->stage = BLOCK_REPLICATION_RUNNING;

    if (s->mode == REPLICATION_MODE_SECONDARY) {
        secondary_do_checkpoint(s, errp);
    }

    s->error = 0;
    aio_context_release(aio_context);
}

static void replication_do_checkpoint(ReplicationState *rs, Error **errp)
{
    BlockDriverState *bs = rs->opaque;
    BDRVReplicationState *s;
    AioContext *aio_context;

    aio_context = bdrv_get_aio_context(bs);
    aio_context_acquire(aio_context);
    s = bs->opaque;

    if (s->stage == BLOCK_REPLICATION_DONE ||
        s->stage == BLOCK_REPLICATION_FAILOVER) {
        /*
         * This case happens when a secondary was promoted to primary.
         * Ignore the request because the secondary side of replication
         * doesn't have to do anything anymore.
         */
        aio_context_release(aio_context);
        return;
    }

    if (s->mode == REPLICATION_MODE_SECONDARY) {
        secondary_do_checkpoint(s, errp);
    }
    aio_context_release(aio_context);
}

static void replication_get_error(ReplicationState *rs, Error **errp)
{
    BlockDriverState *bs = rs->opaque;
    BDRVReplicationState *s;
    AioContext *aio_context;

    aio_context = bdrv_get_aio_context(bs);
    aio_context_acquire(aio_context);
    s = bs->opaque;

    if (s->stage == BLOCK_REPLICATION_NONE) {
        error_setg(errp, "Block replication is not running");
        aio_context_release(aio_context);
        return;
    }

    if (s->error) {
        error_setg(errp, "I/O error occurred");
        aio_context_release(aio_context);
        return;
    }
    aio_context_release(aio_context);
}

static void replication_done(void *opaque, int ret)
{
    BlockDriverState *bs = opaque;
    BDRVReplicationState *s = bs->opaque;

    if (ret == 0) {
        s->stage = BLOCK_REPLICATION_DONE;

        s->active_disk = NULL;
        s->secondary_disk = NULL;
        s->hidden_disk = NULL;
        s->error = 0;
    } else {
        s->stage = BLOCK_REPLICATION_FAILOVER_FAILED;
        s->error = -EIO;
    }
}

static void replication_stop(ReplicationState *rs, bool failover, Error **errp)
{
    BlockDriverState *bs = rs->opaque;
    BDRVReplicationState *s;
    AioContext *aio_context;

    aio_context = bdrv_get_aio_context(bs);
    aio_context_acquire(aio_context);
    s = bs->opaque;

    if (s->stage == BLOCK_REPLICATION_DONE ||
        s->stage == BLOCK_REPLICATION_FAILOVER) {
        /*
         * This case happens when a secondary was promoted to primary.
         * Ignore the request because the secondary side of replication
         * doesn't have to do anything anymore.
         */
        aio_context_release(aio_context);
        return;
    }

    if (s->stage != BLOCK_REPLICATION_RUNNING) {
        error_setg(errp, "Block replication is not running");
        aio_context_release(aio_context);
        return;
    }

    switch (s->mode) {
    case REPLICATION_MODE_PRIMARY:
        s->stage = BLOCK_REPLICATION_DONE;
        s->error = 0;
        break;
    case REPLICATION_MODE_SECONDARY:
        /*
         * This BDS will be closed, and the job should be completed
         * before the BDS is closed, because we will access hidden
         * disk, secondary disk in backup_job_completed().
         */
        if (s->backup_job) {
            job_cancel_sync(&s->backup_job->job);
        }

        if (!failover) {
            secondary_do_checkpoint(s, errp);
            s->stage = BLOCK_REPLICATION_DONE;
            aio_context_release(aio_context);
            return;
        }

        s->stage = BLOCK_REPLICATION_FAILOVER;
        s->commit_job = commit_active_start(
                            NULL, s->active_disk->bs, s->secondary_disk->bs,
                            JOB_INTERNAL, 0, BLOCKDEV_ON_ERROR_REPORT,
                            NULL, replication_done, bs, true, errp);
        break;
    default:
        aio_context_release(aio_context);
        abort();
    }
    aio_context_release(aio_context);
}

static const char *const replication_strong_runtime_opts[] = {
    REPLICATION_MODE,
    REPLICATION_TOP_ID,

    NULL
};

static BlockDriver bdrv_replication = {
    .format_name                = "replication",
    .instance_size              = sizeof(BDRVReplicationState),

    .bdrv_open                  = replication_open,
    .bdrv_close                 = replication_close,
    .bdrv_child_perm            = replication_child_perm,

    .bdrv_getlength             = replication_getlength,
    .bdrv_co_readv              = replication_co_readv,
    .bdrv_co_writev             = replication_co_writev,

    .is_filter                  = true,

    .has_variable_length        = true,
    .strong_runtime_opts        = replication_strong_runtime_opts,
};

static void bdrv_replication_init(void)
{
    bdrv_register(&bdrv_replication);
}

block_init(bdrv_replication_init);
