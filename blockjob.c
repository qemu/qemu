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
#include "block/block.h"
#include "block/blockjob_int.h"
#include "block/block_int.h"
#include "block/trace.h"
#include "sysemu/block-backend.h"
#include "qapi/error.h"
#include "qapi/qapi-events-block-core.h"
#include "qapi/qmp/qerror.h"
#include "qemu/coroutine.h"
#include "qemu/timer.h"

/* Transactional group of block jobs */
struct BlockJobTxn {

    /* Is this txn being cancelled? */
    bool aborting;

    /* List of jobs */
    QLIST_HEAD(, BlockJob) jobs;

    /* Reference count */
    int refcnt;
};

/*
 * The block job API is composed of two categories of functions.
 *
 * The first includes functions used by the monitor.  The monitor is
 * peculiar in that it accesses the block job list with block_job_get, and
 * therefore needs consistency across block_job_get and the actual operation
 * (e.g. block_job_set_speed).  The consistency is achieved with
 * aio_context_acquire/release.  These functions are declared in blockjob.h.
 *
 * The second includes functions used by the block job drivers and sometimes
 * by the core block layer.  These do not care about locking, because the
 * whole coroutine runs under the AioContext lock, and are declared in
 * blockjob_int.h.
 */

static bool is_block_job(Job *job)
{
    return job_type(job) == JOB_TYPE_BACKUP ||
           job_type(job) == JOB_TYPE_COMMIT ||
           job_type(job) == JOB_TYPE_MIRROR ||
           job_type(job) == JOB_TYPE_STREAM;
}

BlockJob *block_job_next(BlockJob *bjob)
{
    Job *job = bjob ? &bjob->job : NULL;

    do {
        job = job_next(job);
    } while (job && !is_block_job(job));

    return job ? container_of(job, BlockJob, job) : NULL;
}

BlockJob *block_job_get(const char *id)
{
    Job *job = job_get(id);

    if (job && is_block_job(job)) {
        return container_of(job, BlockJob, job);
    } else {
        return NULL;
    }
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

void block_job_txn_del_job(BlockJob *job)
{
    if (job->txn) {
        QLIST_REMOVE(job, txn_list);
        block_job_txn_unref(job->txn);
        job->txn = NULL;
    }
}

static void block_job_attached_aio_context(AioContext *new_context,
                                           void *opaque);
static void block_job_detach_aio_context(void *opaque);

void block_job_free(Job *job)
{
    BlockJob *bjob = container_of(job, BlockJob, job);
    BlockDriverState *bs = blk_bs(bjob->blk);

    assert(!bjob->txn);

    bs->job = NULL;
    block_job_remove_all_bdrv(bjob);
    blk_remove_aio_context_notifier(bjob->blk,
                                    block_job_attached_aio_context,
                                    block_job_detach_aio_context, bjob);
    blk_unref(bjob->blk);
    error_free(bjob->blocker);
}

static void block_job_attached_aio_context(AioContext *new_context,
                                           void *opaque)
{
    BlockJob *job = opaque;

    job->job.aio_context = new_context;
    if (job->driver->attached_aio_context) {
        job->driver->attached_aio_context(job, new_context);
    }

    job_resume(&job->job);
}

void block_job_drain(Job *job)
{
    BlockJob *bjob = container_of(job, BlockJob, job);

    blk_drain(bjob->blk);
    if (bjob->driver->drain) {
        bjob->driver->drain(bjob);
    }
}

static void block_job_detach_aio_context(void *opaque)
{
    BlockJob *job = opaque;

    /* In case the job terminates during aio_poll()... */
    job_ref(&job->job);

    job_pause(&job->job);

    while (!job->job.paused && !job_is_completed(&job->job)) {
        job_drain(&job->job);
    }

    job->job.aio_context = NULL;
    job_unref(&job->job);
}

static char *child_job_get_parent_desc(BdrvChild *c)
{
    BlockJob *job = c->opaque;
    return g_strdup_printf("%s job '%s'", job_type_str(&job->job), job->job.id);
}

static void child_job_drained_begin(BdrvChild *c)
{
    BlockJob *job = c->opaque;
    job_pause(&job->job);
}

static void child_job_drained_end(BdrvChild *c)
{
    BlockJob *job = c->opaque;
    job_resume(&job->job);
}

static const BdrvChildRole child_job = {
    .get_parent_desc    = child_job_get_parent_desc,
    .drained_begin      = child_job_drained_begin,
    .drained_end        = child_job_drained_end,
    .stay_at_node       = true,
};

void block_job_remove_all_bdrv(BlockJob *job)
{
    GSList *l;
    for (l = job->nodes; l; l = l->next) {
        BdrvChild *c = l->data;
        bdrv_op_unblock_all(c->bs, job->blocker);
        bdrv_root_unref_child(c);
    }
    g_slist_free(job->nodes);
    job->nodes = NULL;
}

int block_job_add_bdrv(BlockJob *job, const char *name, BlockDriverState *bs,
                       uint64_t perm, uint64_t shared_perm, Error **errp)
{
    BdrvChild *c;

    c = bdrv_root_attach_child(bs, name, &child_job, perm, shared_perm,
                               job, errp);
    if (c == NULL) {
        return -EPERM;
    }

    job->nodes = g_slist_prepend(job->nodes, c);
    bdrv_ref(bs);
    bdrv_op_block_all(bs, job->blocker);

    return 0;
}

bool block_job_is_internal(BlockJob *job)
{
    return (job->job.id == NULL);
}

const BlockJobDriver *block_job_driver(BlockJob *job)
{
    return job->driver;
}

static int block_job_prepare(BlockJob *job)
{
    if (job->job.ret == 0 && job->driver->prepare) {
        job->job.ret = job->driver->prepare(job);
    }
    return job->job.ret;
}

static void job_cancel_async(Job *job, bool force)
{
    if (job->user_paused) {
        /* Do not call job_enter here, the caller will handle it.  */
        job->user_paused = false;
        if (job->driver->user_resume) {
            job->driver->user_resume(job);
        }
        assert(job->pause_count > 0);
        job->pause_count--;
    }
    job->cancelled = true;
    /* To prevent 'force == false' overriding a previous 'force == true' */
    job->force_cancel |= force;
}

static int block_job_txn_apply(BlockJobTxn *txn, int fn(BlockJob *), bool lock)
{
    AioContext *ctx;
    BlockJob *job, *next;
    int rc = 0;

    QLIST_FOREACH_SAFE(job, &txn->jobs, txn_list, next) {
        if (lock) {
            ctx = blk_get_aio_context(job->blk);
            aio_context_acquire(ctx);
        }
        rc = fn(job);
        if (lock) {
            aio_context_release(ctx);
        }
        if (rc) {
            break;
        }
    }
    return rc;
}

static int block_job_finish_sync(BlockJob *job,
                                 void (*finish)(BlockJob *, Error **errp),
                                 Error **errp)
{
    Error *local_err = NULL;
    int ret;

    assert(blk_bs(job->blk)->job == job);

    job_ref(&job->job);

    if (finish) {
        finish(job, &local_err);
    }
    if (local_err) {
        error_propagate(errp, local_err);
        job_unref(&job->job);
        return -EBUSY;
    }
    /* job_drain calls job_enter, and it should be enough to induce progress
     * until the job completes or moves to the main thread.
    */
    while (!job->job.deferred_to_main_loop && !job_is_completed(&job->job)) {
        job_drain(&job->job);
    }
    while (!job_is_completed(&job->job)) {
        aio_poll(qemu_get_aio_context(), true);
    }
    ret = (job_is_cancelled(&job->job) && job->job.ret == 0)
          ? -ECANCELED : job->job.ret;
    job_unref(&job->job);
    return ret;
}

static void block_job_completed_txn_abort(BlockJob *job)
{
    AioContext *ctx;
    BlockJobTxn *txn = job->txn;
    BlockJob *other_job;

    if (txn->aborting) {
        /*
         * We are cancelled by another job, which will handle everything.
         */
        return;
    }
    txn->aborting = true;
    block_job_txn_ref(txn);

    /* We are the first failed job. Cancel other jobs. */
    QLIST_FOREACH(other_job, &txn->jobs, txn_list) {
        ctx = blk_get_aio_context(other_job->blk);
        aio_context_acquire(ctx);
    }

    /* Other jobs are effectively cancelled by us, set the status for
     * them; this job, however, may or may not be cancelled, depending
     * on the caller, so leave it. */
    QLIST_FOREACH(other_job, &txn->jobs, txn_list) {
        if (other_job != job) {
            job_cancel_async(&other_job->job, false);
        }
    }
    while (!QLIST_EMPTY(&txn->jobs)) {
        other_job = QLIST_FIRST(&txn->jobs);
        ctx = blk_get_aio_context(other_job->blk);
        if (!job_is_completed(&other_job->job)) {
            assert(job_is_cancelled(&other_job->job));
            block_job_finish_sync(other_job, NULL, NULL);
        }
        job_finalize_single(&other_job->job);
        aio_context_release(ctx);
    }

    block_job_txn_unref(txn);
}

static int block_job_needs_finalize(BlockJob *job)
{
    return !job->job.auto_finalize;
}

static int block_job_finalize_single(BlockJob *job)
{
    return job_finalize_single(&job->job);
}

static void block_job_do_finalize(BlockJob *job)
{
    int rc;
    assert(job && job->txn);

    /* prepare the transaction to complete */
    rc = block_job_txn_apply(job->txn, block_job_prepare, true);
    if (rc) {
        block_job_completed_txn_abort(job);
    } else {
        block_job_txn_apply(job->txn, block_job_finalize_single, true);
    }
}

static int block_job_transition_to_pending(BlockJob *job)
{
    job_state_transition(&job->job, JOB_STATUS_PENDING);
    if (!job->job.auto_finalize) {
        job_event_pending(&job->job);
    }
    return 0;
}

static void block_job_completed_txn_success(BlockJob *job)
{
    BlockJobTxn *txn = job->txn;
    BlockJob *other_job;

    job_state_transition(&job->job, JOB_STATUS_WAITING);

    /*
     * Successful completion, see if there are other running jobs in this
     * txn.
     */
    QLIST_FOREACH(other_job, &txn->jobs, txn_list) {
        if (!job_is_completed(&other_job->job)) {
            return;
        }
        assert(other_job->job.ret == 0);
    }

    block_job_txn_apply(txn, block_job_transition_to_pending, false);

    /* If no jobs need manual finalization, automatically do so */
    if (block_job_txn_apply(txn, block_job_needs_finalize, false) == 0) {
        block_job_do_finalize(job);
    }
}

/* Assumes the job_mutex is held */
static bool job_timer_pending(Job *job)
{
    return timer_pending(&job->sleep_timer);
}

void block_job_set_speed(BlockJob *job, int64_t speed, Error **errp)
{
    int64_t old_speed = job->speed;

    if (job_apply_verb(&job->job, JOB_VERB_SET_SPEED, errp)) {
        return;
    }
    if (speed < 0) {
        error_setg(errp, QERR_INVALID_PARAMETER, "speed");
        return;
    }

    ratelimit_set_speed(&job->limit, speed, BLOCK_JOB_SLICE_TIME);

    job->speed = speed;
    if (speed && speed <= old_speed) {
        return;
    }

    /* kick only if a timer is pending */
    job_enter_cond(&job->job, job_timer_pending);
}

int64_t block_job_ratelimit_get_delay(BlockJob *job, uint64_t n)
{
    if (!job->speed) {
        return 0;
    }

    return ratelimit_calculate_delay(&job->limit, n);
}

void block_job_finalize(BlockJob *job, Error **errp)
{
    assert(job && job->job.id);
    if (job_apply_verb(&job->job, JOB_VERB_FINALIZE, errp)) {
        return;
    }
    block_job_do_finalize(job);
}

void block_job_dismiss(BlockJob **jobptr, Error **errp)
{
    BlockJob *job = *jobptr;
    /* similarly to _complete, this is QMP-interface only. */
    assert(job->job.id);
    if (job_apply_verb(&job->job, JOB_VERB_DISMISS, errp)) {
        return;
    }

    job_do_dismiss(&job->job);
    *jobptr = NULL;
}

void block_job_cancel(BlockJob *job, bool force)
{
    if (job->job.status == JOB_STATUS_CONCLUDED) {
        job_do_dismiss(&job->job);
        return;
    }
    job_cancel_async(&job->job, force);
    if (!job_started(&job->job)) {
        block_job_completed(job, -ECANCELED);
    } else if (job->job.deferred_to_main_loop) {
        block_job_completed_txn_abort(job);
    } else {
        block_job_enter(job);
    }
}

void block_job_user_cancel(BlockJob *job, bool force, Error **errp)
{
    if (job_apply_verb(&job->job, JOB_VERB_CANCEL, errp)) {
        return;
    }
    block_job_cancel(job, force);
}

/* A wrapper around block_job_cancel() taking an Error ** parameter so it may be
 * used with block_job_finish_sync() without the need for (rather nasty)
 * function pointer casts there. */
static void block_job_cancel_err(BlockJob *job, Error **errp)
{
    block_job_cancel(job, false);
}

int block_job_cancel_sync(BlockJob *job)
{
    return block_job_finish_sync(job, &block_job_cancel_err, NULL);
}

void block_job_cancel_sync_all(void)
{
    BlockJob *job;
    AioContext *aio_context;

    while ((job = block_job_next(NULL))) {
        aio_context = blk_get_aio_context(job->blk);
        aio_context_acquire(aio_context);
        block_job_cancel_sync(job);
        aio_context_release(aio_context);
    }
}

static void block_job_complete(BlockJob *job, Error **errp)
{
    job_complete(&job->job, errp);
}

int block_job_complete_sync(BlockJob *job, Error **errp)
{
    return block_job_finish_sync(job, &block_job_complete, errp);
}

void block_job_progress_update(BlockJob *job, uint64_t done)
{
    job->offset += done;
}

void block_job_progress_set_remaining(BlockJob *job, uint64_t remaining)
{
    job->len = job->offset + remaining;
}

BlockJobInfo *block_job_query(BlockJob *job, Error **errp)
{
    BlockJobInfo *info;

    if (block_job_is_internal(job)) {
        error_setg(errp, "Cannot query QEMU internal jobs");
        return NULL;
    }
    info = g_new0(BlockJobInfo, 1);
    info->type      = g_strdup(job_type_str(&job->job));
    info->device    = g_strdup(job->job.id);
    info->len       = job->len;
    info->busy      = atomic_read(&job->job.busy);
    info->paused    = job->job.pause_count > 0;
    info->offset    = job->offset;
    info->speed     = job->speed;
    info->io_status = job->iostatus;
    info->ready     = job->ready;
    info->status    = job->job.status;
    info->auto_finalize = job->job.auto_finalize;
    info->auto_dismiss  = job->job.auto_dismiss;
    info->has_error = job->job.ret != 0;
    info->error     = job->job.ret ? g_strdup(strerror(-job->job.ret)) : NULL;
    return info;
}

static void block_job_iostatus_set_err(BlockJob *job, int error)
{
    if (job->iostatus == BLOCK_DEVICE_IO_STATUS_OK) {
        job->iostatus = error == ENOSPC ? BLOCK_DEVICE_IO_STATUS_NOSPACE :
                                          BLOCK_DEVICE_IO_STATUS_FAILED;
    }
}

static void block_job_event_cancelled(Notifier *n, void *opaque)
{
    BlockJob *job = opaque;

    if (block_job_is_internal(job)) {
        return;
    }

    qapi_event_send_block_job_cancelled(job_type(&job->job),
                                        job->job.id,
                                        job->len,
                                        job->offset,
                                        job->speed,
                                        &error_abort);
}

static void block_job_event_completed(Notifier *n, void *opaque)
{
    BlockJob *job = opaque;
    const char *msg = NULL;

    if (block_job_is_internal(job)) {
        return;
    }

    if (job->job.ret < 0) {
        msg = strerror(-job->job.ret);
    }

    qapi_event_send_block_job_completed(job_type(&job->job),
                                        job->job.id,
                                        job->len,
                                        job->offset,
                                        job->speed,
                                        !!msg,
                                        msg,
                                        &error_abort);
}

static void block_job_event_pending(Notifier *n, void *opaque)
{
    BlockJob *job = opaque;

    if (block_job_is_internal(job)) {
        return;
    }

    qapi_event_send_block_job_pending(job_type(&job->job),
                                      job->job.id,
                                      &error_abort);
}

/*
 * API for block job drivers and the block layer.  These functions are
 * declared in blockjob_int.h.
 */

void *block_job_create(const char *job_id, const BlockJobDriver *driver,
                       BlockJobTxn *txn, BlockDriverState *bs, uint64_t perm,
                       uint64_t shared_perm, int64_t speed, int flags,
                       BlockCompletionFunc *cb, void *opaque, Error **errp)
{
    BlockBackend *blk;
    BlockJob *job;
    int ret;

    if (bs->job) {
        error_setg(errp, QERR_DEVICE_IN_USE, bdrv_get_device_name(bs));
        return NULL;
    }

    if (job_id == NULL && !(flags & JOB_INTERNAL)) {
        job_id = bdrv_get_device_name(bs);
    }

    blk = blk_new(perm, shared_perm);
    ret = blk_insert_bs(blk, bs, errp);
    if (ret < 0) {
        blk_unref(blk);
        return NULL;
    }

    job = job_create(job_id, &driver->job_driver, blk_get_aio_context(blk),
                     flags, cb, opaque, errp);
    if (job == NULL) {
        blk_unref(blk);
        return NULL;
    }

    assert(is_block_job(&job->job));
    assert(job->job.driver->free == &block_job_free);
    assert(job->job.driver->user_resume == &block_job_user_resume);
    assert(job->job.driver->drain == &block_job_drain);

    job->driver        = driver;
    job->blk           = blk;

    job->finalize_cancelled_notifier.notify = block_job_event_cancelled;
    job->finalize_completed_notifier.notify = block_job_event_completed;
    job->pending_notifier.notify = block_job_event_pending;

    notifier_list_add(&job->job.on_finalize_cancelled,
                      &job->finalize_cancelled_notifier);
    notifier_list_add(&job->job.on_finalize_completed,
                      &job->finalize_completed_notifier);
    notifier_list_add(&job->job.on_pending, &job->pending_notifier);

    error_setg(&job->blocker, "block device is in use by block job: %s",
               job_type_str(&job->job));
    block_job_add_bdrv(job, "main node", bs, 0, BLK_PERM_ALL, &error_abort);
    bs->job = job;

    bdrv_op_unblock(bs, BLOCK_OP_TYPE_DATAPLANE, job->blocker);

    blk_add_aio_context_notifier(blk, block_job_attached_aio_context,
                                 block_job_detach_aio_context, job);

    /* Only set speed when necessary to avoid NotSupported error */
    if (speed != 0) {
        Error *local_err = NULL;

        block_job_set_speed(job, speed, &local_err);
        if (local_err) {
            job_early_fail(&job->job);
            error_propagate(errp, local_err);
            return NULL;
        }
    }

    /* Single jobs are modeled as single-job transactions for sake of
     * consolidating the job management logic */
    if (!txn) {
        txn = block_job_txn_new();
        block_job_txn_add_job(txn, job);
        block_job_txn_unref(txn);
    } else {
        block_job_txn_add_job(txn, job);
    }

    return job;
}

void block_job_completed(BlockJob *job, int ret)
{
    assert(job && job->txn && !job_is_completed(&job->job));
    assert(blk_bs(job->blk)->job == job);
    job->job.ret = ret;
    job_update_rc(&job->job);
    trace_block_job_completed(job, ret, job->job.ret);
    if (job->job.ret) {
        block_job_completed_txn_abort(job);
    } else {
        block_job_completed_txn_success(job);
    }
}

void block_job_enter(BlockJob *job)
{
    job_enter_cond(&job->job, NULL);
}

void block_job_yield(BlockJob *job)
{
    assert(job->job.busy);

    /* Check cancellation *before* setting busy = false, too!  */
    if (job_is_cancelled(&job->job)) {
        return;
    }

    if (!job_should_pause(&job->job)) {
        job_do_yield(&job->job, -1);
    }

    job_pause_point(&job->job);
}

void block_job_iostatus_reset(BlockJob *job)
{
    if (job->iostatus == BLOCK_DEVICE_IO_STATUS_OK) {
        return;
    }
    assert(job->job.user_paused && job->job.pause_count > 0);
    job->iostatus = BLOCK_DEVICE_IO_STATUS_OK;
}

void block_job_user_resume(Job *job)
{
    BlockJob *bjob = container_of(job, BlockJob, job);
    block_job_iostatus_reset(bjob);
}

void block_job_event_ready(BlockJob *job)
{
    job_state_transition(&job->job, JOB_STATUS_READY);
    job->ready = true;

    if (block_job_is_internal(job)) {
        return;
    }

    qapi_event_send_block_job_ready(job_type(&job->job),
                                    job->job.id,
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
    case BLOCKDEV_ON_ERROR_AUTO:
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
    if (!block_job_is_internal(job)) {
        qapi_event_send_block_job_error(job->job.id,
                                        is_read ? IO_OPERATION_TYPE_READ :
                                        IO_OPERATION_TYPE_WRITE,
                                        action, &error_abort);
    }
    if (action == BLOCK_ERROR_ACTION_STOP) {
        job_pause(&job->job);
        /* make the pause user visible, which will be resumed from QMP. */
        job->job.user_paused = true;
        block_job_iostatus_set_err(job, error);
    }
    return action;
}
