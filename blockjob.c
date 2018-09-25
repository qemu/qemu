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

static void block_job_attached_aio_context(AioContext *new_context,
                                           void *opaque);
static void block_job_detach_aio_context(void *opaque);

void block_job_free(Job *job)
{
    BlockJob *bjob = container_of(job, BlockJob, job);
    BlockDriverState *bs = blk_bs(bjob->blk);

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
    const JobDriver *drv = job->job.driver;
    BlockJobDriver *bjdrv = container_of(drv, BlockJobDriver, job_driver);

    job->job.aio_context = new_context;
    if (bjdrv->attached_aio_context) {
        bjdrv->attached_aio_context(job, new_context);
    }

    job_resume(&job->job);
}

void block_job_drain(Job *job)
{
    BlockJob *bjob = container_of(job, BlockJob, job);
    const JobDriver *drv = job->driver;
    BlockJobDriver *bjdrv = container_of(drv, BlockJobDriver, job_driver);

    blk_drain(bjob->blk);
    if (bjdrv->drain) {
        bjdrv->drain(bjob);
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

static bool child_job_drained_poll(BdrvChild *c)
{
    BlockJob *bjob = c->opaque;
    Job *job = &bjob->job;
    const BlockJobDriver *drv = block_job_driver(bjob);

    /* An inactive or completed job doesn't have any pending requests. Jobs
     * with !job->busy are either already paused or have a pause point after
     * being reentered, so no job driver code will run before they pause. */
    if (!job->busy || job_is_completed(job)) {
        return false;
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

static const BdrvChildRole child_job = {
    .get_parent_desc    = child_job_get_parent_desc,
    .drained_begin      = child_job_drained_begin,
    .drained_poll       = child_job_drained_poll,
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

static void block_job_on_idle(Notifier *n, void *opaque)
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
    info->busy      = atomic_read(&job->job.busy);
    info->paused    = job->job.pause_count > 0;
    info->offset    = job->job.progress_current;
    info->len       = job->job.progress_total;
    info->speed     = job->speed;
    info->io_status = job->iostatus;
    info->ready     = job_is_ready(&job->job),
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
                                        job->job.progress_total,
                                        job->job.progress_current,
                                        job->speed);
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
                                        job->job.progress_total,
                                        job->job.progress_current,
                                        job->speed,
                                        !!msg,
                                        msg);
}

static void block_job_event_pending(Notifier *n, void *opaque)
{
    BlockJob *job = opaque;

    if (block_job_is_internal(job)) {
        return;
    }

    qapi_event_send_block_job_pending(job_type(&job->job),
                                      job->job.id);
}

static void block_job_event_ready(Notifier *n, void *opaque)
{
    BlockJob *job = opaque;

    if (block_job_is_internal(job)) {
        return;
    }

    qapi_event_send_block_job_ready(job_type(&job->job),
                                    job->job.id,
                                    job->job.progress_total,
                                    job->job.progress_current,
                                    job->speed);
}


/*
 * API for block job drivers and the block layer.  These functions are
 * declared in blockjob_int.h.
 */

void *block_job_create(const char *job_id, const BlockJobDriver *driver,
                       JobTxn *txn, BlockDriverState *bs, uint64_t perm,
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

    job = job_create(job_id, &driver->job_driver, txn, blk_get_aio_context(blk),
                     flags, cb, opaque, errp);
    if (job == NULL) {
        blk_unref(blk);
        return NULL;
    }

    assert(is_block_job(&job->job));
    assert(job->job.driver->free == &block_job_free);
    assert(job->job.driver->user_resume == &block_job_user_resume);
    assert(job->job.driver->drain == &block_job_drain);

    job->blk = blk;

    job->finalize_cancelled_notifier.notify = block_job_event_cancelled;
    job->finalize_completed_notifier.notify = block_job_event_completed;
    job->pending_notifier.notify = block_job_event_pending;
    job->ready_notifier.notify = block_job_event_ready;
    job->idle_notifier.notify = block_job_on_idle;

    notifier_list_add(&job->job.on_finalize_cancelled,
                      &job->finalize_cancelled_notifier);
    notifier_list_add(&job->job.on_finalize_completed,
                      &job->finalize_completed_notifier);
    notifier_list_add(&job->job.on_pending, &job->pending_notifier);
    notifier_list_add(&job->job.on_ready, &job->ready_notifier);
    notifier_list_add(&job->job.on_idle, &job->idle_notifier);

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

    return job;
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
                                        action);
    }
    if (action == BLOCK_ERROR_ACTION_STOP) {
        job_pause(&job->job);
        /* make the pause user visible, which will be resumed from QMP. */
        job->job.user_paused = true;
        block_job_iostatus_set_err(job, error);
    }
    return action;
}
