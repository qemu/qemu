/*
 * QEMU System Emulator block driver
 *
 * Copyright (c) 2011 IBM Corp.
 * Copyright (c) 2012 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "trace.h"
#include "block/block.h"
#include "block/blockjob.h"
#include "block/block_int.h"
#include "sysemu/block-backend.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qjson.h"
#include "qemu/coroutine.h"
#include "qmp-commands.h"
#include "qemu/timer.h"
#include "qapi-event.h"

/* Transactional group of block jobs */
struct BlockJobTxn {

    /* Is this txn being cancelled? */
    bool aborting;

    /* List of jobs */
    QLIST_HEAD(, BlockJob) jobs;

    /* Reference count */
    int refcnt;
};

void *block_job_create(const BlockJobDriver *driver, BlockDriverState *bs,
                       int64_t speed, BlockCompletionFunc *cb,
                       void *opaque, Error **errp)
{
    BlockJob *job;

    if (bs->job) {
        error_setg(errp, QERR_DEVICE_IN_USE, bdrv_get_device_name(bs));
        return NULL;
    }
    bdrv_ref(bs);
    job = g_malloc0(driver->instance_size);
    error_setg(&job->blocker, "block device is in use by block job: %s",
               BlockJobType_lookup[driver->job_type]);
    bdrv_op_block_all(bs, job->blocker);
    bdrv_op_unblock(bs, BLOCK_OP_TYPE_DATAPLANE, job->blocker);

    job->driver        = driver;
    job->id            = g_strdup(bdrv_get_device_name(bs));
    job->bs            = bs;
    job->cb            = cb;
    job->opaque        = opaque;
    job->busy          = true;
    job->refcnt        = 1;
    bs->job = job;

    /* Only set speed when necessary to avoid NotSupported error */
    if (speed != 0) {
        Error *local_err = NULL;

        block_job_set_speed(job, speed, &local_err);
        if (local_err) {
            block_job_unref(job);
            error_propagate(errp, local_err);
            return NULL;
        }
    }
    return job;
}

void block_job_ref(BlockJob *job)
{
    ++job->refcnt;
}

void block_job_unref(BlockJob *job)
{
    if (--job->refcnt == 0) {
        job->bs->job = NULL;
        bdrv_op_unblock_all(job->bs, job->blocker);
        bdrv_unref(job->bs);
        error_free(job->blocker);
        g_free(job->id);
        g_free(job);
    }
}

static void block_job_completed_single(BlockJob *job)
{
    if (!job->ret) {
        if (job->driver->commit) {
            job->driver->commit(job);
        }
    } else {
        if (job->driver->abort) {
            job->driver->abort(job);
        }
    }
    job->cb(job->opaque, job->ret);
    if (job->txn) {
        block_job_txn_unref(job->txn);
    }
    block_job_unref(job);
}

static void block_job_completed_txn_abort(BlockJob *job)
{
    AioContext *ctx;
    BlockJobTxn *txn = job->txn;
    BlockJob *other_job, *next;

    if (txn->aborting) {
        /*
         * We are cancelled by another job, which will handle everything.
         */
        return;
    }
    txn->aborting = true;
    /* We are the first failed job. Cancel other jobs. */
    QLIST_FOREACH(other_job, &txn->jobs, txn_list) {
        ctx = bdrv_get_aio_context(other_job->bs);
        aio_context_acquire(ctx);
    }
    QLIST_FOREACH(other_job, &txn->jobs, txn_list) {
        if (other_job == job || other_job->completed) {
            /* Other jobs are "effectively" cancelled by us, set the status for
             * them; this job, however, may or may not be cancelled, depending
             * on the caller, so leave it. */
            if (other_job != job) {
                other_job->cancelled = true;
            }
            continue;
        }
        block_job_cancel_sync(other_job);
        assert(other_job->completed);
    }
    QLIST_FOREACH_SAFE(other_job, &txn->jobs, txn_list, next) {
        ctx = bdrv_get_aio_context(other_job->bs);
        block_job_completed_single(other_job);
        aio_context_release(ctx);
    }
}

static void block_job_completed_txn_success(BlockJob *job)
{
    AioContext *ctx;
    BlockJobTxn *txn = job->txn;
    BlockJob *other_job, *next;
    /*
     * Successful completion, see if there are other running jobs in this
     * txn.
     */
    QLIST_FOREACH(other_job, &txn->jobs, txn_list) {
        if (!other_job->completed) {
            return;
        }
    }
    /* We are the last completed job, commit the transaction. */
    QLIST_FOREACH_SAFE(other_job, &txn->jobs, txn_list, next) {
        ctx = bdrv_get_aio_context(other_job->bs);
        aio_context_acquire(ctx);
        assert(other_job->ret == 0);
        block_job_completed_single(other_job);
        aio_context_release(ctx);
    }
}

void block_job_completed(BlockJob *job, int ret)
{
    BlockDriverState *bs = job->bs;

    assert(bs->job == job);
    assert(!job->completed);
    job->completed = true;
    job->ret = ret;
    if (!job->txn) {
        block_job_completed_single(job);
    } else if (ret < 0 || block_job_is_cancelled(job)) {
        block_job_completed_txn_abort(job);
    } else {
        block_job_completed_txn_success(job);
    }
}

void block_job_set_speed(BlockJob *job, int64_t speed, Error **errp)
{
    Error *local_err = NULL;

    if (!job->driver->set_speed) {
        error_setg(errp, QERR_UNSUPPORTED);
        return;
    }
    job->driver->set_speed(job, speed, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    job->speed = speed;
}

void block_job_complete(BlockJob *job, Error **errp)
{
    if (job->pause_count || job->cancelled || !job->driver->complete) {
        error_setg(errp, QERR_BLOCK_JOB_NOT_READY, job->id);
        return;
    }

    job->driver->complete(job, errp);
}

void block_job_pause(BlockJob *job)
{
    job->pause_count++;
}

bool block_job_is_paused(BlockJob *job)
{
    return job->pause_count > 0;
}

void block_job_resume(BlockJob *job)
{
    assert(job->pause_count > 0);
    job->pause_count--;
    if (job->pause_count) {
        return;
    }
    block_job_enter(job);
}

void block_job_enter(BlockJob *job)
{
    block_job_iostatus_reset(job);
    if (job->co && !job->busy) {
        qemu_coroutine_enter(job->co, NULL);
    }
}

void block_job_cancel(BlockJob *job)
{
    job->cancelled = true;
    block_job_enter(job);
}

bool block_job_is_cancelled(BlockJob *job)
{
    return job->cancelled;
}

void block_job_iostatus_reset(BlockJob *job)
{
    job->iostatus = BLOCK_DEVICE_IO_STATUS_OK;
    if (job->driver->iostatus_reset) {
        job->driver->iostatus_reset(job);
    }
}

static int block_job_finish_sync(BlockJob *job,
                                 void (*finish)(BlockJob *, Error **errp),
                                 Error **errp)
{
    BlockDriverState *bs = job->bs;
    Error *local_err = NULL;
    int ret;

    assert(bs->job == job);

    block_job_ref(job);
    finish(job, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        block_job_unref(job);
        return -EBUSY;
    }
    while (!job->completed) {
        aio_poll(job->deferred_to_main_loop ? qemu_get_aio_context() :
                                              bdrv_get_aio_context(bs),
                 true);
    }
    ret = (job->cancelled && job->ret == 0) ? -ECANCELED : job->ret;
    block_job_unref(job);
    return ret;
}

/* A wrapper around block_job_cancel() taking an Error ** parameter so it may be
 * used with block_job_finish_sync() without the need for (rather nasty)
 * function pointer casts there. */
static void block_job_cancel_err(BlockJob *job, Error **errp)
{
    block_job_cancel(job);
}

int block_job_cancel_sync(BlockJob *job)
{
    return block_job_finish_sync(job, &block_job_cancel_err, NULL);
}

int block_job_complete_sync(BlockJob *job, Error **errp)
{
    return block_job_finish_sync(job, &block_job_complete, errp);
}

void block_job_sleep_ns(BlockJob *job, QEMUClockType type, int64_t ns)
{
    assert(job->busy);

    /* Check cancellation *before* setting busy = false, too!  */
    if (block_job_is_cancelled(job)) {
        return;
    }

    job->busy = false;
    if (block_job_is_paused(job)) {
        qemu_coroutine_yield();
    } else {
        co_aio_sleep_ns(bdrv_get_aio_context(job->bs), type, ns);
    }
    job->busy = true;
}

void block_job_yield(BlockJob *job)
{
    assert(job->busy);

    /* Check cancellation *before* setting busy = false, too!  */
    if (block_job_is_cancelled(job)) {
        return;
    }

    job->busy = false;
    qemu_coroutine_yield();
    job->busy = true;
}

BlockJobInfo *block_job_query(BlockJob *job)
{
    BlockJobInfo *info = g_new0(BlockJobInfo, 1);
    info->type      = g_strdup(BlockJobType_lookup[job->driver->job_type]);
    info->device    = g_strdup(job->id);
    info->len       = job->len;
    info->busy      = job->busy;
    info->paused    = job->pause_count > 0;
    info->offset    = job->offset;
    info->speed     = job->speed;
    info->io_status = job->iostatus;
    info->ready     = job->ready;
    return info;
}

static void block_job_iostatus_set_err(BlockJob *job, int error)
{
    if (job->iostatus == BLOCK_DEVICE_IO_STATUS_OK) {
        job->iostatus = error == ENOSPC ? BLOCK_DEVICE_IO_STATUS_NOSPACE :
                                          BLOCK_DEVICE_IO_STATUS_FAILED;
    }
}

void block_job_event_cancelled(BlockJob *job)
{
    qapi_event_send_block_job_cancelled(job->driver->job_type,
                                        job->id,
                                        job->len,
                                        job->offset,
                                        job->speed,
                                        &error_abort);
}

void block_job_event_completed(BlockJob *job, const char *msg)
{
    qapi_event_send_block_job_completed(job->driver->job_type,
                                        job->id,
                                        job->len,
                                        job->offset,
                                        job->speed,
                                        !!msg,
                                        msg,
                                        &error_abort);
}

void block_job_event_ready(BlockJob *job)
{
    job->ready = true;

    qapi_event_send_block_job_ready(job->driver->job_type,
                                    job->id,
                                    job->len,
                                    job->offset,
                                    job->speed, &error_abort);
}

BlockErrorAction block_job_error_action(BlockJob *job, BlockdevOnError on_err,
                                        int is_read, int error)
{
    BlockErrorAction action;

    switch (on_err) {
    case BLOCKDEV_ON_ERROR_ENOSPC:
        action = (error == ENOSPC) ?
                 BLOCK_ERROR_ACTION_STOP : BLOCK_ERROR_ACTION_REPORT;
        break;
    case BLOCKDEV_ON_ERROR_STOP:
        action = BLOCK_ERROR_ACTION_STOP;
        break;
    case BLOCKDEV_ON_ERROR_REPORT:
        action = BLOCK_ERROR_ACTION_REPORT;
        break;
    case BLOCKDEV_ON_ERROR_IGNORE:
        action = BLOCK_ERROR_ACTION_IGNORE;
        break;
    default:
        abort();
    }
    qapi_event_send_block_job_error(job->id,
                                    is_read ? IO_OPERATION_TYPE_READ :
                                    IO_OPERATION_TYPE_WRITE,
                                    action, &error_abort);
    if (action == BLOCK_ERROR_ACTION_STOP) {
        /* make the pause user visible, which will be resumed from QMP. */
        job->user_paused = true;
        block_job_pause(job);
        block_job_iostatus_set_err(job, error);
    }
    return action;
}

typedef struct {
    BlockJob *job;
    QEMUBH *bh;
    AioContext *aio_context;
    BlockJobDeferToMainLoopFn *fn;
    void *opaque;
} BlockJobDeferToMainLoopData;

static void block_job_defer_to_main_loop_bh(void *opaque)
{
    BlockJobDeferToMainLoopData *data = opaque;
    AioContext *aio_context;

    qemu_bh_delete(data->bh);

    /* Prevent race with block_job_defer_to_main_loop() */
    aio_context_acquire(data->aio_context);

    /* Fetch BDS AioContext again, in case it has changed */
    aio_context = bdrv_get_aio_context(data->job->bs);
    aio_context_acquire(aio_context);

    data->job->deferred_to_main_loop = false;
    data->fn(data->job, data->opaque);

    aio_context_release(aio_context);

    aio_context_release(data->aio_context);

    g_free(data);
}

void block_job_defer_to_main_loop(BlockJob *job,
                                  BlockJobDeferToMainLoopFn *fn,
                                  void *opaque)
{
    BlockJobDeferToMainLoopData *data = g_malloc(sizeof(*data));
    data->job = job;
    data->bh = qemu_bh_new(block_job_defer_to_main_loop_bh, data);
    data->aio_context = bdrv_get_aio_context(job->bs);
    data->fn = fn;
    data->opaque = opaque;
    job->deferred_to_main_loop = true;

    qemu_bh_schedule(data->bh);
}

BlockJobTxn *block_job_txn_new(void)
{
    BlockJobTxn *txn = g_new0(BlockJobTxn, 1);
    QLIST_INIT(&txn->jobs);
    txn->refcnt = 1;
    return txn;
}

static void block_job_txn_ref(BlockJobTxn *txn)
{
    txn->refcnt++;
}

void block_job_txn_unref(BlockJobTxn *txn)
{
    if (txn && --txn->refcnt == 0) {
        g_free(txn);
    }
}

void block_job_txn_add_job(BlockJobTxn *txn, BlockJob *job)
{
    if (!txn) {
        return;
    }

    assert(!job->txn);
    job->txn = txn;

    QLIST_INSERT_HEAD(&txn->jobs, job, txn_list);
    block_job_txn_ref(txn);
}
