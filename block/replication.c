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
#include "system/block-backend.h"
#include "qapi/error.h"
#include "qobject/qdict.h"
#include "block/replication.h"

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

    ret = bdrv_open_file_child(NULL, options, "file", bs, errp);
    if (ret < 0) {
        return ret;
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
    GLOBAL_STATE_CODE();

    if (s->stage == BLOCK_REPLICATION_RUNNING) {
        replication_stop(s->rs, false, NULL);
    }
    if (s->stage == BLOCK_REPLICATION_FAILOVER) {
        commit_job = &s->commit_job->job;
        assert(commit_job->aio_context == qemu_get_current_aio_context());
        job_cancel_sync(commit_job, false);
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
    if (role & BDRV_CHILD_PRIMARY) {
        *nperm = BLK_PERM_CONSISTENT_READ;
    } else {
        *nperm = 0;
    }

    if ((bs->open_flags & (BDRV_O_INACTIVE | BDRV_O_RDWR)) == BDRV_O_RDWR) {
        *nperm |= BLK_PERM_WRITE;
    }
    *nshared = BLK_PERM_CONSISTENT_READ
               | BLK_PERM_WRITE
               | BLK_PERM_WRITE_UNCHANGED;
}

static int64_t coroutine_fn GRAPH_RDLOCK
replication_co_getlength(BlockDriverState *bs)
{
    return bdrv_co_getlength(bs->file->bs);
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

static int coroutine_fn GRAPH_RDLOCK
replication_co_readv(BlockDriverState *bs, int64_t sector_num,
                     int remaining_sectors, QEMUIOVector *qiov)
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

static int coroutine_fn GRAPH_RDLOCK
replication_co_writev(BlockDriverState *bs, int64_t sector_num,
                      int remaining_sectors, QEMUIOVector *qiov, int flags)
{
    BDRVReplicationState *s = bs->opaque;
    QEMUIOVector hd_qiov;
    uint64_t bytes_done = 0;
    BdrvChild *top = bs->file;
    BdrvChild *base = s->secondary_disk;
    BdrvChild *target;
    int ret;
    int64_t n;

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

        ret = bdrv_co_is_allocated_above(top->bs, base->bs, false,
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

static void GRAPH_UNLOCKED
secondary_do_checkpoint(BlockDriverState *bs, Error **errp)
{
    BDRVReplicationState *s = bs->opaque;
    BdrvChild *active_disk;
    Error *local_err = NULL;
    int ret;

    GRAPH_RDLOCK_GUARD_MAINLOOP();

    if (!s->backup_job) {
        error_setg(errp, "Backup job was cancelled unexpectedly");
        return;
    }

    backup_do_checkpoint(s->backup_job, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    active_disk = bs->file;
    if (!active_disk->bs->drv) {
        error_setg(errp, "Active disk %s is ejected",
                   active_disk->bs->node_name);
        return;
    }

    ret = bdrv_make_empty(active_disk, errp);
    if (ret < 0) {
        return;
    }

    if (!s->hidden_disk->bs->drv) {
        error_setg(errp, "Hidden disk %s is ejected",
                   s->hidden_disk->bs->node_name);
        return;
    }

    ret = bdrv_make_empty(s->hidden_disk, errp);
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
    BdrvChild *hidden_disk, *secondary_disk;
    BlockReopenQueue *reopen_queue = NULL;

    GLOBAL_STATE_CODE();

    bdrv_graph_rdlock_main_loop();
    /*
     * s->hidden_disk and s->secondary_disk may not be set yet, as they will
     * only be set after the children are writable.
     */
    hidden_disk = bs->file->bs->backing;
    secondary_disk = hidden_disk->bs->backing;
    bdrv_graph_rdunlock_main_loop();

    if (writable) {
        s->orig_hidden_read_only = bdrv_is_read_only(hidden_disk->bs);
        s->orig_secondary_read_only = bdrv_is_read_only(secondary_disk->bs);
    }

    if (s->orig_hidden_read_only) {
        QDict *opts = qdict_new();
        qdict_put_bool(opts, BDRV_OPT_READ_ONLY, !writable);
        reopen_queue = bdrv_reopen_queue(reopen_queue, hidden_disk->bs,
                                         opts, true);
    }

    if (s->orig_secondary_read_only) {
        QDict *opts = qdict_new();
        qdict_put_bool(opts, BDRV_OPT_READ_ONLY, !writable);
        reopen_queue = bdrv_reopen_queue(reopen_queue, secondary_disk->bs,
                                         opts, true);
    }

    if (reopen_queue) {
        bdrv_reopen_multiple(reopen_queue, errp);
    }
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

static bool GRAPH_RDLOCK
check_top_bs(BlockDriverState *top_bs, BlockDriverState *bs)
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
    BdrvChild *active_disk, *hidden_disk, *secondary_disk;
    int64_t active_length, hidden_length, disk_length;
    Error *local_err = NULL;
    BackupPerf perf = { .use_copy_range = true, .max_workers = 1 };

    GLOBAL_STATE_CODE();

    s = bs->opaque;

    if (s->stage == BLOCK_REPLICATION_DONE ||
        s->stage == BLOCK_REPLICATION_FAILOVER) {
        /*
         * This case happens when a secondary is promoted to primary.
         * Ignore the request because the secondary side of replication
         * doesn't have to do anything anymore.
         */
        return;
    }

    if (s->stage != BLOCK_REPLICATION_NONE) {
        error_setg(errp, "Block replication is running or done");
        return;
    }

    if (s->mode != mode) {
        error_setg(errp, "The parameter mode's value is invalid, needs %d,"
                   " but got %d", s->mode, mode);
        return;
    }

    switch (s->mode) {
    case REPLICATION_MODE_PRIMARY:
        break;
    case REPLICATION_MODE_SECONDARY:
        bdrv_graph_rdlock_main_loop();
        active_disk = bs->file;
        if (!active_disk || !active_disk->bs || !active_disk->bs->backing) {
            error_setg(errp, "Active disk doesn't have backing file");
            bdrv_graph_rdunlock_main_loop();
            return;
        }

        hidden_disk = active_disk->bs->backing;
        if (!hidden_disk->bs || !hidden_disk->bs->backing) {
            error_setg(errp, "Hidden disk doesn't have backing file");
            bdrv_graph_rdunlock_main_loop();
            return;
        }

        secondary_disk = hidden_disk->bs->backing;
        if (!secondary_disk->bs || !bdrv_has_blk(secondary_disk->bs)) {
            error_setg(errp, "The secondary disk doesn't have block backend");
            bdrv_graph_rdunlock_main_loop();
            return;
        }
        bdrv_graph_rdunlock_main_loop();

        /* verify the length */
        active_length = bdrv_getlength(active_disk->bs);
        hidden_length = bdrv_getlength(hidden_disk->bs);
        disk_length = bdrv_getlength(secondary_disk->bs);
        if (active_length < 0 || hidden_length < 0 || disk_length < 0 ||
            active_length != hidden_length || hidden_length != disk_length) {
            error_setg(errp, "Active disk, hidden disk, secondary disk's length"
                       " are not the same");
            return;
        }

        /* Must be true, or the bdrv_getlength() calls would have failed */
        assert(active_disk->bs->drv && hidden_disk->bs->drv);

        bdrv_graph_rdlock_main_loop();
        if (!active_disk->bs->drv->bdrv_make_empty ||
            !hidden_disk->bs->drv->bdrv_make_empty) {
            error_setg(errp,
                       "Active disk or hidden disk doesn't support make_empty");
            bdrv_graph_rdunlock_main_loop();
            return;
        }
        bdrv_graph_rdunlock_main_loop();

        /* reopen the backing file in r/w mode */
        reopen_backing_file(bs, true, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }

        bdrv_graph_wrlock_drained();

        bdrv_ref(hidden_disk->bs);
        s->hidden_disk = bdrv_attach_child(bs, hidden_disk->bs, "hidden disk",
                                           &child_of_bds, BDRV_CHILD_DATA,
                                           &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            bdrv_graph_wrunlock();
            return;
        }

        bdrv_ref(secondary_disk->bs);
        s->secondary_disk = bdrv_attach_child(bs, secondary_disk->bs,
                                              "secondary disk", &child_of_bds,
                                              BDRV_CHILD_DATA, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            bdrv_graph_wrunlock();
            return;
        }

        /* start backup job now */
        error_setg(&s->blocker,
                   "Block device is in use by internal backup job");

        top_bs = bdrv_lookup_bs(s->top_id, s->top_id, NULL);
        if (!top_bs || !bdrv_is_root_node(top_bs) ||
            !check_top_bs(top_bs, bs)) {
            error_setg(errp, "No top_bs or it is invalid");
            bdrv_graph_wrunlock();
            reopen_backing_file(bs, false, NULL);
            return;
        }
        bdrv_op_block_all(top_bs, s->blocker);

        bdrv_graph_wrunlock();

        s->backup_job = backup_job_create(
                                NULL, s->secondary_disk->bs, s->hidden_disk->bs,
                                0, MIRROR_SYNC_MODE_NONE, NULL, 0, false, false,
                                NULL, &perf,
                                BLOCKDEV_ON_ERROR_REPORT,
                                BLOCKDEV_ON_ERROR_REPORT,
                                ON_CBW_ERROR_BREAK_GUEST_WRITE,
                                JOB_INTERNAL,
                                backup_job_completed, bs, NULL, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            backup_job_cleanup(bs);
            return;
        }
        job_start(&s->backup_job->job);
        break;
    default:
        abort();
    }

    s->stage = BLOCK_REPLICATION_RUNNING;

    if (s->mode == REPLICATION_MODE_SECONDARY) {
        secondary_do_checkpoint(bs, errp);
    }

    s->error = 0;
}

static void replication_do_checkpoint(ReplicationState *rs, Error **errp)
{
    BlockDriverState *bs = rs->opaque;
    BDRVReplicationState *s = bs->opaque;

    if (s->stage == BLOCK_REPLICATION_DONE ||
        s->stage == BLOCK_REPLICATION_FAILOVER) {
        /*
         * This case happens when a secondary was promoted to primary.
         * Ignore the request because the secondary side of replication
         * doesn't have to do anything anymore.
         */
        return;
    }

    if (s->mode == REPLICATION_MODE_SECONDARY) {
        secondary_do_checkpoint(bs, errp);
    }
}

static void replication_get_error(ReplicationState *rs, Error **errp)
{
    BlockDriverState *bs = rs->opaque;
    BDRVReplicationState *s = bs->opaque;

    if (s->stage == BLOCK_REPLICATION_NONE) {
        error_setg(errp, "Block replication is not running");
        return;
    }

    if (s->error) {
        error_setg(errp, "I/O error occurred");
        return;
    }
}

static void replication_done(void *opaque, int ret)
{
    BlockDriverState *bs = opaque;
    BDRVReplicationState *s = bs->opaque;

    if (ret == 0) {
        s->stage = BLOCK_REPLICATION_DONE;

        bdrv_graph_wrlock_drained();
        bdrv_unref_child(bs, s->secondary_disk);
        s->secondary_disk = NULL;
        bdrv_unref_child(bs, s->hidden_disk);
        s->hidden_disk = NULL;
        bdrv_graph_wrunlock();

        s->error = 0;
    } else {
        s->stage = BLOCK_REPLICATION_FAILOVER_FAILED;
        s->error = -EIO;
    }
}

static void replication_stop(ReplicationState *rs, bool failover, Error **errp)
{
    BlockDriverState *bs = rs->opaque;
    BDRVReplicationState *s = bs->opaque;

    if (s->stage == BLOCK_REPLICATION_DONE ||
        s->stage == BLOCK_REPLICATION_FAILOVER) {
        /*
         * This case happens when a secondary was promoted to primary.
         * Ignore the request because the secondary side of replication
         * doesn't have to do anything anymore.
         */
        return;
    }

    if (s->stage != BLOCK_REPLICATION_RUNNING) {
        error_setg(errp, "Block replication is not running");
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
            job_cancel_sync(&s->backup_job->job, true);
        }

        if (!failover) {
            secondary_do_checkpoint(bs, errp);
            s->stage = BLOCK_REPLICATION_DONE;
            return;
        }

        bdrv_graph_rdlock_main_loop();
        s->stage = BLOCK_REPLICATION_FAILOVER;
        s->commit_job = commit_active_start(
                            NULL, bs->file->bs, s->secondary_disk->bs,
                            JOB_INTERNAL, 0, BLOCKDEV_ON_ERROR_REPORT,
                            NULL, replication_done, bs, true, errp);
        bdrv_graph_rdunlock_main_loop();
        break;
    default:
        abort();
    }
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

    .bdrv_co_getlength          = replication_co_getlength,
    .bdrv_co_readv              = replication_co_readv,
    .bdrv_co_writev             = replication_co_writev,

    .is_filter                  = true,

    .strong_runtime_opts        = replication_strong_runtime_opts,
};

static void bdrv_replication_init(void)
{
    bdrv_register(&bdrv_replication);
}

block_init(bdrv_replication_init);
