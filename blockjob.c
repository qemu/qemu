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
#include "block/aio-wait.h"
#include "block/block.h"
#include "block/blockjob_int.h"
#include "block/block_int.h"
#include "block/trace.h"
#include "sysemu/block-backend.h"
#include "qapi/error.h"
#include "qapi/qapi-events-block-core.h"
#include "qapi/qmp/qerror.h"
#include "qemu/main-loop.h"
#include "qemu/timer.h"

static bool is_block_job(Job *job)
{
    return job_type(job) == JOB_TYPE_BACKUP ||
           job_type(job) == JOB_TYPE_COMMIT ||
           job_type(job) == JOB_TYPE_MIRROR ||
           job_type(job) == JOB_TYPE_STREAM;
}

BlockJob *block_job_next_locked(BlockJob *bjob)
{
    Job *job = bjob ? &bjob->job : NULL;
    GLOBAL_STATE_CODE();

    do {
        job = job_next_locked(job);
    } while (job && !is_block_job(job));

    return job ? container_of(job, BlockJob, job) : NULL;
}

BlockJob *block_job_get_locked(const char *id)
{
    Job *job = job_get_locked(id);
    GLOBAL_STATE_CODE();

    if (job && is_block_job(job)) {
        return container_of(job, BlockJob, job);
    } else {
        return NULL;
    }
}

BlockJob *block_job_get(const char *id)
{
    JOB_LOCK_GUARD();
    return block_job_get_locked(id);
}

void block_job_free(Job *job)
{
    BlockJob *bjob = container_of(job, BlockJob, job);
    GLOBAL_STATE_CODE();

    block_job_remove_all_bdrv(bjob);
    ratelimit_destroy(&bjob->limit);
    error_free(bjob->blocker);
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

static bool child_job_drained_poll(BdrvChild *c)
{
    BlockJob *bjob = c->opaque;
    Job *job = &bjob->job;
    const BlockJobDriver *drv = block_job_driver(bjob);

    /* An inactive or completed job doesn't have any pending requests. Jobs
     * with !job->busy are either already paused or have a pause point after
     * being reentered, so no job driver code will run before they pause. */
    WITH_JOB_LOCK_GUARD() {
        if (!job->busy || job_is_completed_locked(job)) {
            return false;
        }
    }

    /* Otherwise, assume that it isn't fully stopped yet, but allow the job to
     * override this assumption. */
    if (drv->drained_poll) {
        return drv->drained_poll(bjob);
    } else {
        return true;
    }
}

static void child_job_drained_end(BdrvChild *c)
{
    BlockJob *job = c->opaque;
    job_resume(&job->job);
}

typedef struct BdrvStateChildJobContext {
    AioContext *new_ctx;
    BlockJob *job;
} BdrvStateChildJobContext;

static void child_job_set_aio_ctx_commit(void *opaque)
{
    BdrvStateChildJobContext *s = opaque;
    BlockJob *job = s->job;

    job_set_aio_context(&job->job, s->new_ctx);
}

static TransactionActionDrv change_child_job_context = {
    .commit = child_job_set_aio_ctx_commit,
    .clean = g_free,
};

static bool child_job_change_aio_ctx(BdrvChild *c, AioContext *ctx,
                                     GHashTable *visited, Transaction *tran,
                                     Error **errp)
{
    BlockJob *job = c->opaque;
    BdrvStateChildJobContext *s;
    GSList *l;

    for (l = job->nodes; l; l = l->next) {
        BdrvChild *sibling = l->data;
        if (!bdrv_child_change_aio_context(sibling, ctx, visited,
                                           tran, errp)) {
            return false;
        }
    }

    s = g_new(BdrvStateChildJobContext, 1);
    *s = (BdrvStateChildJobContext) {
        .new_ctx = ctx,
        .job = job,
    };

    tran_add(tran, &change_child_job_context, s);
    return true;
}

static AioContext *child_job_get_parent_aio_context(BdrvChild *c)
{
    BlockJob *job = c->opaque;
    IO_CODE();
    JOB_LOCK_GUARD();

    return job->job.aio_context;
}

static const BdrvChildClass child_job = {
    .get_parent_desc    = child_job_get_parent_desc,
    .drained_begin      = child_job_drained_begin,
    .drained_poll       = child_job_drained_poll,
    .drained_end        = child_job_drained_end,
    .change_aio_ctx     = child_job_change_aio_ctx,
    .stay_at_node       = true,
    .get_parent_aio_context = child_job_get_parent_aio_context,
};

void block_job_remove_all_bdrv(BlockJob *job)
{
    GLOBAL_STATE_CODE();
    /*
     * bdrv_root_unref_child() may reach child_job_[can_]set_aio_ctx(),
     * which will also traverse job->nodes, so consume the list one by
     * one to make sure that such a concurrent access does not attempt
     * to process an already freed BdrvChild.
     */
    aio_context_release(job->job.aio_context);
    bdrv_graph_wrlock(NULL);
    aio_context_acquire(job->job.aio_context);
    while (job->nodes) {
        GSList *l = job->nodes;
        BdrvChild *c = l->data;

        job->nodes = l->next;

        bdrv_op_unblock_all(c->bs, job->blocker);
        bdrv_root_unref_child(c);

        g_slist_free_1(l);
    }
    bdrv_graph_wrunlock_ctx(job->job.aio_context);
}

bool block_job_has_bdrv(BlockJob *job, BlockDriverState *bs)
{
    GSList *el;
    GLOBAL_STATE_CODE();

    for (el = job->nodes; el; el = el->next) {
        BdrvChild *c = el->data;
        if (c->bs == bs) {
            return true;
        }
    }

    return false;
}

int block_job_add_bdrv(BlockJob *job, const char *name, BlockDriverState *bs,
                       uint64_t perm, uint64_t shared_perm, Error **errp)
{
    BdrvChild *c;
    AioContext *ctx = bdrv_get_aio_context(bs);
    bool need_context_ops;
    GLOBAL_STATE_CODE();

    bdrv_ref(bs);

    need_context_ops = ctx != job->job.aio_context;

    if (need_context_ops) {
        if (job->job.aio_context != qemu_get_aio_context()) {
            aio_context_release(job->job.aio_context);
        }
        aio_context_acquire(ctx);
    }
    c = bdrv_root_attach_child(bs, name, &child_job, 0, perm, shared_perm, job,
                               errp);
    if (need_context_ops) {
        aio_context_release(ctx);
        if (job->job.aio_context != qemu_get_aio_context()) {
            aio_context_acquire(job->job.aio_context);
        }
    }
    if (c == NULL) {
        return -EPERM;
    }

    job->nodes = g_slist_prepend(job->nodes, c);
    bdrv_op_block_all(bs, job->blocker);

    return 0;
}

/* Called with job_mutex lock held. */
static void block_job_on_idle_locked(Notifier *n, void *opaque)
{
    aio_wait_kick();
}

bool block_job_is_internal(BlockJob *job)
{
    return (job->job.id == NULL);
}

const BlockJobDriver *block_job_driver(BlockJob *job)
{
    return container_of(job->job.driver, BlockJobDriver, job_driver);
}

/* Assumes the job_mutex is held */
static bool job_timer_pending(Job *job)
{
    return timer_pending(&job->sleep_timer);
}

bool block_job_set_speed_locked(BlockJob *job, int64_t speed, Error **errp)
{
    const BlockJobDriver *drv = block_job_driver(job);
    int64_t old_speed = job->speed;

    GLOBAL_STATE_CODE();

    if (job_apply_verb_locked(&job->job, JOB_VERB_SET_SPEED, errp) < 0) {
        return false;
    }
    if (speed < 0) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "speed",
                   "a non-negative value");
        return false;
    }

    ratelimit_set_speed(&job->limit, speed, BLOCK_JOB_SLICE_TIME);

    job->speed = speed;

    if (drv->set_speed) {
        job_unlock();
        drv->set_speed(job, speed);
        job_lock();
    }

    if (speed && speed <= old_speed) {
        return true;
    }

    /* kick only if a timer is pending */
    job_enter_cond_locked(&job->job, job_timer_pending);

    return true;
}

static bool block_job_set_speed(BlockJob *job, int64_t speed, Error **errp)
{
    JOB_LOCK_GUARD();
    return block_job_set_speed_locked(job, speed, errp);
}

void block_job_change_locked(BlockJob *job, BlockJobChangeOptions *opts,
                             Error **errp)
{
    const BlockJobDriver *drv = block_job_driver(job);

    GLOBAL_STATE_CODE();

    if (job_apply_verb_locked(&job->job, JOB_VERB_CHANGE, errp)) {
        return;
    }

    if (drv->change) {
        job_unlock();
        drv->change(job, opts, errp);
        job_lock();
    } else {
        error_setg(errp, "Job type does not support change");
    }
}

void block_job_ratelimit_processed_bytes(BlockJob *job, uint64_t n)
{
    IO_CODE();
    ratelimit_calculate_delay(&job->limit, n);
}

void block_job_ratelimit_sleep(BlockJob *job)
{
    uint64_t delay_ns;

    /*
     * Sleep at least once. If the job is reentered early, keep waiting until
     * we've waited for the full time that is necessary to keep the job at the
     * right speed.
     *
     * Make sure to recalculate the delay after each (possibly interrupted)
     * sleep because the speed can change while the job has yielded.
     */
    do {
        delay_ns = ratelimit_calculate_delay(&job->limit, 0);
        job_sleep_ns(&job->job, delay_ns);
    } while (delay_ns && !job_is_cancelled(&job->job));
}

BlockJobInfo *block_job_query_locked(BlockJob *job, Error **errp)
{
    BlockJobInfo *info;
    uint64_t progress_current, progress_total;
    const BlockJobDriver *drv = block_job_driver(job);

    GLOBAL_STATE_CODE();

    if (block_job_is_internal(job)) {
        error_setg(errp, "Cannot query QEMU internal jobs");
        return NULL;
    }

    progress_get_snapshot(&job->job.progress, &progress_current,
                          &progress_total);

    info = g_new0(BlockJobInfo, 1);
    info->type      = job_type(&job->job);
    info->device    = g_strdup(job->job.id);
    info->busy      = job->job.busy;
    info->paused    = job->job.pause_count > 0;
    info->offset    = progress_current;
    info->len       = progress_total;
    info->speed     = job->speed;
    info->io_status = job->iostatus;
    info->ready     = job_is_ready_locked(&job->job),
    info->status    = job->job.status;
    info->auto_finalize = job->job.auto_finalize;
    info->auto_dismiss  = job->job.auto_dismiss;
    if (job->job.ret) {
        info->error = job->job.err ?
                        g_strdup(error_get_pretty(job->job.err)) :
                        g_strdup(strerror(-job->job.ret));
    }
    if (drv->query) {
        job_unlock();
        drv->query(job, info);
        job_lock();
    }
    return info;
}

/* Called with job lock held */
static void block_job_iostatus_set_err_locked(BlockJob *job, int error)
{
    if (job->iostatus == BLOCK_DEVICE_IO_STATUS_OK) {
        job->iostatus = error == ENOSPC ? BLOCK_DEVICE_IO_STATUS_NOSPACE :
                                          BLOCK_DEVICE_IO_STATUS_FAILED;
    }
}

/* Called with job_mutex lock held. */
static void block_job_event_cancelled_locked(Notifier *n, void *opaque)
{
    BlockJob *job = opaque;
    uint64_t progress_current, progress_total;

    if (block_job_is_internal(job)) {
        return;
    }

    progress_get_snapshot(&job->job.progress, &progress_current,
                          &progress_total);

    qapi_event_send_block_job_cancelled(job_type(&job->job),
                                        job->job.id,
                                        progress_total,
                                        progress_current,
                                        job->speed);
}

/* Called with job_mutex lock held. */
static void block_job_event_completed_locked(Notifier *n, void *opaque)
{
    BlockJob *job = opaque;
    const char *msg = NULL;
    uint64_t progress_current, progress_total;

    if (block_job_is_internal(job)) {
        return;
    }

    if (job->job.ret < 0) {
        msg = error_get_pretty(job->job.err);
    }

    progress_get_snapshot(&job->job.progress, &progress_current,
                          &progress_total);

    qapi_event_send_block_job_completed(job_type(&job->job),
                                        job->job.id,
                                        progress_total,
                                        progress_current,
                                        job->speed,
                                        msg);
}

/* Called with job_mutex lock held. */
static void block_job_event_pending_locked(Notifier *n, void *opaque)
{
    BlockJob *job = opaque;

    if (block_job_is_internal(job)) {
        return;
    }

    qapi_event_send_block_job_pending(job_type(&job->job),
                                      job->job.id);
}

/* Called with job_mutex lock held. */
static void block_job_event_ready_locked(Notifier *n, void *opaque)
{
    BlockJob *job = opaque;
    uint64_t progress_current, progress_total;

    if (block_job_is_internal(job)) {
        return;
    }

    progress_get_snapshot(&job->job.progress, &progress_current,
                          &progress_total);

    qapi_event_send_block_job_ready(job_type(&job->job),
                                    job->job.id,
                                    progress_total,
                                    progress_current,
                                    job->speed);
}


void *block_job_create(const char *job_id, const BlockJobDriver *driver,
                       JobTxn *txn, BlockDriverState *bs, uint64_t perm,
                       uint64_t shared_perm, int64_t speed, int flags,
                       BlockCompletionFunc *cb, void *opaque, Error **errp)
{
    BlockJob *job;
    int ret;
    GLOBAL_STATE_CODE();

    bdrv_graph_wrlock(bs);

    if (job_id == NULL && !(flags & JOB_INTERNAL)) {
        job_id = bdrv_get_device_name(bs);
    }

    job = job_create(job_id, &driver->job_driver, txn, bdrv_get_aio_context(bs),
                     flags, cb, opaque, errp);
    if (job == NULL) {
        bdrv_graph_wrunlock(bs);
        return NULL;
    }

    assert(is_block_job(&job->job));
    assert(job->job.driver->free == &block_job_free);
    assert(job->job.driver->user_resume == &block_job_user_resume);

    ratelimit_init(&job->limit);

    job->finalize_cancelled_notifier.notify = block_job_event_cancelled_locked;
    job->finalize_completed_notifier.notify = block_job_event_completed_locked;
    job->pending_notifier.notify = block_job_event_pending_locked;
    job->ready_notifier.notify = block_job_event_ready_locked;
    job->idle_notifier.notify = block_job_on_idle_locked;

    WITH_JOB_LOCK_GUARD() {
        notifier_list_add(&job->job.on_finalize_cancelled,
                          &job->finalize_cancelled_notifier);
        notifier_list_add(&job->job.on_finalize_completed,
                          &job->finalize_completed_notifier);
        notifier_list_add(&job->job.on_pending, &job->pending_notifier);
        notifier_list_add(&job->job.on_ready, &job->ready_notifier);
        notifier_list_add(&job->job.on_idle, &job->idle_notifier);
    }

    error_setg(&job->blocker, "block device is in use by block job: %s",
               job_type_str(&job->job));

    ret = block_job_add_bdrv(job, "main node", bs, perm, shared_perm, errp);
    if (ret < 0) {
        goto fail;
    }

    bdrv_op_unblock(bs, BLOCK_OP_TYPE_DATAPLANE, job->blocker);

    if (!block_job_set_speed(job, speed, errp)) {
        goto fail;
    }

    bdrv_graph_wrunlock(bs);
    return job;

fail:
    bdrv_graph_wrunlock(bs);
    job_early_fail(&job->job);
    return NULL;
}

void block_job_iostatus_reset_locked(BlockJob *job)
{
    GLOBAL_STATE_CODE();
    if (job->iostatus == BLOCK_DEVICE_IO_STATUS_OK) {
        return;
    }
    assert(job->job.user_paused && job->job.pause_count > 0);
    job->iostatus = BLOCK_DEVICE_IO_STATUS_OK;
}

static void block_job_iostatus_reset(BlockJob *job)
{
    JOB_LOCK_GUARD();
    block_job_iostatus_reset_locked(job);
}

void block_job_user_resume(Job *job)
{
    BlockJob *bjob = container_of(job, BlockJob, job);
    GLOBAL_STATE_CODE();
    block_job_iostatus_reset(bjob);
}

BlockErrorAction block_job_error_action(BlockJob *job, BlockdevOnError on_err,
                                        int is_read, int error)
{
    BlockErrorAction action;
    IO_CODE();

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
                                        action);
    }
    if (action == BLOCK_ERROR_ACTION_STOP) {
        WITH_JOB_LOCK_GUARD() {
            if (!job->job.user_paused) {
                job_pause_locked(&job->job);
                /*
                 * make the pause user visible, which will be
                 * resumed from QMP.
                 */
                job->job.user_paused = true;
            }
            block_job_iostatus_set_err_locked(job, error);
        }
    }
    return action;
}

AioContext *block_job_get_aio_context(BlockJob *job)
{
    GLOBAL_STATE_CODE();
    return job->job.aio_context;
}
