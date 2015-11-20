/*
 * Replication Block filter
 *
 * Copyright (c) 2015 HUAWEI TECHNOLOGIES CO., LTD.
 * Copyright (c) 2015 Intel Corporation
 * Copyright (c) 2015 FUJITSU LIMITED
 *
 * Author:
 *   Wen Congyang <wency@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu-common.h"
#include "block/block_int.h"
#include "block/blockjob.h"
#include "block/nbd.h"

typedef struct BDRVReplicationState {
    ReplicationMode mode;
    int replication_state;
    BlockDriverState *active_disk;
    BlockDriverState *hidden_disk;
    BlockDriverState *secondary_disk;
    BlockDriverState *top_bs;
    Error *blocker;
    int orig_hidden_flags;
    int orig_secondary_flags;
    int error;
} BDRVReplicationState;

enum {
    BLOCK_REPLICATION_NONE,     /* block replication is not started */
    BLOCK_REPLICATION_RUNNING,  /* block replication is running */
    BLOCK_REPLICATION_DONE,     /* block replication is done(failover) */
};

#define COMMIT_CLUSTER_BITS 16
#define COMMIT_CLUSTER_SIZE (1 << COMMIT_CLUSTER_BITS)
#define COMMIT_SECTORS_PER_CLUSTER (COMMIT_CLUSTER_SIZE / BDRV_SECTOR_SIZE)

static void replication_stop(BlockDriverState *bs, bool failover, Error **errp);

#define REPLICATION_MODE        "mode"
static QemuOptsList replication_runtime_opts = {
    .name = "replication",
    .head = QTAILQ_HEAD_INITIALIZER(replication_runtime_opts.head),
    .desc = {
        {
            .name = REPLICATION_MODE,
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

static int replication_open(BlockDriverState *bs, QDict *options,
                            int flags, Error **errp)
{
    int ret;
    BDRVReplicationState *s = bs->opaque;;
    Error *local_err = NULL;
    QemuOpts *opts = NULL;
    const char *mode;

    ret = -EINVAL;
    opts = qemu_opts_create(&replication_runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        goto fail;
    }

    mode = qemu_opt_get(opts, REPLICATION_MODE);
    if (!mode) {
        error_setg(&local_err, "Missing the option mode");
        goto fail;
    }

    if (!strcmp(mode, "primary")) {
        s->mode = REPLICATION_MODE_PRIMARY;
    } else if (!strcmp(mode, "secondary")) {
        s->mode = REPLICATION_MODE_SECONDARY;
    } else {
        error_setg(&local_err,
                   "The option mode's value should be primary or secondary");
        goto fail;
    }

    ret = 0;

fail:
    qemu_opts_del(opts);
    /* propagate error */
    if (local_err) {
        error_propagate(errp, local_err);
    }
    return ret;
}

static void replication_close(BlockDriverState *bs)
{
    BDRVReplicationState *s = bs->opaque;

    if (s->replication_state == BLOCK_REPLICATION_RUNNING) {
        replication_stop(bs, false, NULL);
    }
}

static int64_t replication_getlength(BlockDriverState *bs)
{
    return bdrv_getlength(bs->file->bs);
}

static int replication_get_io_status(BDRVReplicationState *s)
{
    switch (s->replication_state) {
    case BLOCK_REPLICATION_NONE:
        return -EIO;
    case BLOCK_REPLICATION_RUNNING:
        return 0;
    case BLOCK_REPLICATION_DONE:
        return s->mode == REPLICATION_MODE_PRIMARY ? -EIO : 1;
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

    /*
     * After failover, because we don't commit active disk/hidden disk
     * to secondary disk, so we should read from active disk directly.
     */
    ret = bdrv_co_readv(bs->file->bs, sector_num, remaining_sectors, qiov);
    return replication_return_value(s, ret);
}

static coroutine_fn int replication_co_writev(BlockDriverState *bs,
                                              int64_t sector_num,
                                              int remaining_sectors,
                                              QEMUIOVector *qiov)
{
    BDRVReplicationState *s = bs->opaque;
    QEMUIOVector hd_qiov;
    uint64_t bytes_done = 0;
    BlockDriverState *top = bs->file->bs;
    BlockDriverState *base = s->secondary_disk;
    BlockDriverState *target;
    int ret, n;

    ret = replication_get_io_status(s);
    if (ret < 0) {
        return ret;
    }

    if (ret == 0) {
        ret = bdrv_co_writev(bs->file->bs, sector_num,
                             remaining_sectors, qiov);
        return replication_return_value(s, ret);
    }

    /*
     * Only write to active disk if the sectors have
     * already been allocated in active disk/hidden disk.
     */
    qemu_iovec_init(&hd_qiov, qiov->niov);
    while (remaining_sectors > 0) {
        ret = bdrv_is_allocated_above(top, base, sector_num,
                                      remaining_sectors, &n);
        if (ret < 0) {
            return ret;
        }

        qemu_iovec_reset(&hd_qiov);
        qemu_iovec_concat(&hd_qiov, qiov, bytes_done, n * 512);

        target = ret ? top : base;
        ret = bdrv_co_writev(target, sector_num, n, &hd_qiov);
        if (ret < 0) {
            return ret;
        }

        remaining_sectors -= n;
        sector_num += n;
        bytes_done += n * BDRV_SECTOR_SIZE;
    }

    return 0;
}

static coroutine_fn int replication_co_discard(BlockDriverState *bs,
                                               int64_t sector_num,
                                               int nb_sectors)
{
    BDRVReplicationState *s = bs->opaque;
    int ret;

    ret = replication_get_io_status(s);
    if (ret < 0) {
        return ret;
    }

    if (ret == 1) {
        /* It is secondary qemu and we are after failover */
        ret = bdrv_co_discard(s->secondary_disk, sector_num, nb_sectors);
        if (ret) {
            return ret;
        }
    }

    ret = bdrv_co_discard(bs->file->bs, sector_num, nb_sectors);
    return replication_return_value(s, ret);
}

static bool replication_recurse_is_first_non_filter(BlockDriverState *bs,
                                                    BlockDriverState *candidate)
{
    return bdrv_recurse_is_first_non_filter(bs->file->bs, candidate);
}

static void secondary_do_checkpoint(BDRVReplicationState *s, Error **errp)
{
    Error *local_err = NULL;
    int ret;

    if (!s->secondary_disk->job) {
        error_setg(errp, "Backup job is cancelled unexpectedly");
        return;
    }

    block_job_do_checkpoint(s->secondary_disk->job, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    ret = s->active_disk->drv->bdrv_make_empty(s->active_disk);
    if (ret < 0) {
        error_setg(errp, "Cannot make active disk empty");
        return;
    }

    ret = s->hidden_disk->drv->bdrv_make_empty(s->hidden_disk);
    if (ret < 0) {
        error_setg(errp, "Cannot make hidden disk empty");
        return;
    }
}

static void reopen_backing_file(BDRVReplicationState *s, bool writable,
                                Error **errp)
{
    BlockReopenQueue *reopen_queue = NULL;
    int orig_hidden_flags, orig_secondary_flags;
    int new_hidden_flags, new_secondary_flags;
    Error *local_err = NULL;

    if (writable) {
        orig_hidden_flags = bdrv_get_flags(s->hidden_disk);
        new_hidden_flags = orig_hidden_flags | BDRV_O_RDWR;
        orig_secondary_flags = bdrv_get_flags(s->secondary_disk);
        new_secondary_flags = orig_secondary_flags | BDRV_O_RDWR;
    } else {
        orig_hidden_flags = s->orig_hidden_flags | BDRV_O_RDWR;
        new_hidden_flags = s->orig_hidden_flags;
        orig_secondary_flags = s->orig_secondary_flags | BDRV_O_RDWR;
        new_secondary_flags = s->orig_secondary_flags;
    }

    if (orig_hidden_flags != new_hidden_flags) {
        reopen_queue = bdrv_reopen_queue(reopen_queue, s->hidden_disk, NULL,
                                         new_hidden_flags);
    }

    if (!(orig_secondary_flags & BDRV_O_RDWR)) {
        reopen_queue = bdrv_reopen_queue(reopen_queue, s->secondary_disk, NULL,
                                         new_secondary_flags);
    }

    if (reopen_queue) {
        bdrv_reopen_multiple(reopen_queue, &local_err);
        if (local_err != NULL) {
            error_propagate(errp, local_err);
        }
    }
}

static void backup_job_cleanup(BDRVReplicationState *s)
{
    bdrv_op_unblock_all(s->top_bs, s->blocker);
    error_free(s->blocker);
    reopen_backing_file(s, false, NULL);
}

static void backup_job_completed(void *opaque, int ret)
{
    BDRVReplicationState *s = opaque;

    if (s->replication_state != BLOCK_REPLICATION_DONE) {
        /* The backup job is cancelled unexpectedly */
        s->error = -EIO;
    }

    backup_job_cleanup(s);
}

static BlockDriverState *get_top_bs(BlockDriverState *bs)
{
    BdrvChild *child;

    while (!bs->blk) {
        if (QLIST_EMPTY(&bs->parents)) {
            return NULL;
        }

        child = QLIST_FIRST(&bs->parents);
        if (QLIST_NEXT(child, next_parent)) {
            return NULL;
        }

        bs = child->parent;
    }

    return bs;
}

static void replication_start(BlockDriverState *bs, ReplicationMode mode,
                              Error **errp)
{
    BDRVReplicationState *s = bs->opaque;
    int64_t active_length, hidden_length, disk_length;
    AioContext *aio_context;
    Error *local_err = NULL;

    if (s->replication_state != BLOCK_REPLICATION_NONE) {
        error_setg(errp, "Block replication is running or done");
        return;
    }

    if (s->mode != mode) {
        error_setg(errp, "The parameter mode's value is invalid, needs %d,"
                   " but receives %d", s->mode, mode);
        return;
    }

    switch (s->mode) {
    case REPLICATION_MODE_PRIMARY:
        break;
    case REPLICATION_MODE_SECONDARY:
        s->active_disk = bs->file->bs;
        if (!bs->file->bs->backing) {
            error_setg(errp, "Active disk doesn't have backing file");
            return;
        }

        s->hidden_disk = s->active_disk->backing->bs;
        if (!s->hidden_disk->backing) {
            error_setg(errp, "Hidden disk doesn't have backing file");
            return;
        }

        s->secondary_disk = s->hidden_disk->backing->bs;
        if (!s->secondary_disk->blk) {
            error_setg(errp, "The secondary disk doesn't have block backend");
            return;
        }

        s->top_bs = get_top_bs(bs);
        if (!s->top_bs) {
            error_setg(errp, "Cannot get the top block driver state to do"
                       " internal backup");
            return;
        }

        /* verify the length */
        active_length = bdrv_getlength(s->active_disk);
        hidden_length = bdrv_getlength(s->hidden_disk);
        disk_length = bdrv_getlength(s->secondary_disk);
        if (active_length < 0 || hidden_length < 0 || disk_length < 0 ||
            active_length != hidden_length || hidden_length != disk_length) {
            error_setg(errp, "active disk, hidden disk, secondary disk's length"
                       " are not the same");
            return;
        }

        if (!s->active_disk->drv->bdrv_make_empty ||
            !s->hidden_disk->drv->bdrv_make_empty) {
            error_setg(errp,
                       "active disk or hidden disk doesn't support make_empty");
            return;
        }

        /* reopen the backing file in r/w mode */
        reopen_backing_file(s, true, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }

        /* start backup job now */
        error_setg(&s->blocker,
                   "block device is in use by internal backup job");
        bdrv_op_block_all(s->top_bs, s->blocker);
        bdrv_op_unblock(s->top_bs, BLOCK_OP_TYPE_DATAPLANE, s->blocker);
        bdrv_ref(s->hidden_disk);

        aio_context = bdrv_get_aio_context(bs);
        aio_context_acquire(aio_context);
        backup_start(s->secondary_disk, s->hidden_disk, 0,
                     MIRROR_SYNC_MODE_NONE, NULL, BLOCKDEV_ON_ERROR_REPORT,
                     BLOCKDEV_ON_ERROR_REPORT, backup_job_completed,
                     s, NULL, &local_err);
        aio_context_release(aio_context);
        if (local_err) {
            error_propagate(errp, local_err);
            backup_job_cleanup(s);
            bdrv_unref(s->hidden_disk);
            return;
        }
        break;
    default:
        abort();
    }

    s->replication_state = BLOCK_REPLICATION_RUNNING;

    if (s->mode == REPLICATION_MODE_SECONDARY) {
        secondary_do_checkpoint(s, errp);
    }

    s->error = 0;
}

static void replication_do_checkpoint(BlockDriverState *bs, Error **errp)
{
    BDRVReplicationState *s = bs->opaque;

    if (s->replication_state != BLOCK_REPLICATION_RUNNING) {
        error_setg(errp, "Block replication is not running");
        return;
    }

    if (s->error) {
        error_setg(errp, "I/O error occurs");
        return;
    }

    if (s->mode == REPLICATION_MODE_SECONDARY) {
        secondary_do_checkpoint(s, errp);
    }
}

static void replication_stop(BlockDriverState *bs, bool failover, Error **errp)
{
    BDRVReplicationState *s = bs->opaque;

    if (s->replication_state != BLOCK_REPLICATION_RUNNING) {
        error_setg(errp, "Block replication is not running");
        return;
    }

    s->replication_state = BLOCK_REPLICATION_DONE;

    switch (s->mode) {
    case REPLICATION_MODE_PRIMARY:
        break;
    case REPLICATION_MODE_SECONDARY:
        if (!failover) {
            /*
             * This BDS will be closed, and the job should be completed
             * before the BDS is closed, because we will access hidden
             * disk, secondary disk in backup_job_completed().
             */
            if (s->secondary_disk->job) {
                block_job_cancel_sync(s->secondary_disk->job);
            }
            secondary_do_checkpoint(s, errp);
            return;
        }

        if (s->secondary_disk->job) {
            block_job_cancel(s->secondary_disk->job);
        }
        break;
    default:
        abort();
    }
}

BlockDriver bdrv_replication = {
    .format_name                = "replication",
    .protocol_name              = "replication",
    .instance_size              = sizeof(BDRVReplicationState),

    .bdrv_open                  = replication_open,
    .bdrv_close                 = replication_close,

    .bdrv_getlength             = replication_getlength,
    .bdrv_co_readv              = replication_co_readv,
    .bdrv_co_writev             = replication_co_writev,
    .bdrv_co_discard            = replication_co_discard,

    .is_filter                  = true,
    .bdrv_recurse_is_first_non_filter = replication_recurse_is_first_non_filter,

    .bdrv_start_replication     = replication_start,
    .bdrv_do_checkpoint         = replication_do_checkpoint,
    .bdrv_stop_replication      = replication_stop,

    .has_variable_length        = true,
};

static void bdrv_replication_init(void)
{
    bdrv_register(&bdrv_replication);
}

block_init(bdrv_replication_init);
