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

#include "config-host.h"
#include "qemu-common.h"
#include "trace.h"
#include "block/block.h"
#include "block/blockjob.h"
#include "block/block_int.h"
#include "qapi/qmp/qjson.h"
#include "block/coroutine.h"
#include "qmp-commands.h"
#include "qemu/timer.h"
#include "qapi-event.h"

void *block_job_create(const BlockJobDriver *driver, BlockDriverState *bs,
                       int64_t speed, BlockDriverCompletionFunc *cb,
                       void *opaque, Error **errp)
{
    BlockJob *job;

    if (bs->job) {
        error_set(errp, QERR_DEVICE_IN_USE, bdrv_get_device_name(bs));
        return NULL;
    }
    bdrv_ref(bs);
    job = g_malloc0(driver->instance_size);
    error_setg(&job->blocker, "block device is in use by block job: %s",
               BlockJobType_lookup[driver->job_type]);
    bdrv_op_block_all(bs, job->blocker);

    job->driver        = driver;
    job->bs            = bs;
    job->cb            = cb;
    job->opaque        = opaque;
    job->busy          = true;
    bs->job = job;

    /* Only set speed when necessary to avoid NotSupported error */
    if (speed != 0) {
        Error *local_err = NULL;

        block_job_set_speed(job, speed, &local_err);
        if (local_err) {
            bs->job = NULL;
            bdrv_op_unblock_all(bs, job->blocker);
            error_free(job->blocker);
            g_free(job);
            error_propagate(errp, local_err);
            return NULL;
        }
    }
    return job;
}

void block_job_completed(BlockJob *job, int ret)
{
    BlockDriverState *bs = job->bs;

    assert(bs->job == job);
    job->cb(job->opaque, ret);
    bs->job = NULL;
    bdrv_op_unblock_all(bs, job->blocker);
    error_free(job->blocker);
    g_free(job);
}

void block_job_set_speed(BlockJob *job, int64_t speed, Error **errp)
{
    Error *local_err = NULL;

    if (!job->driver->set_speed) {
        error_set(errp, QERR_UNSUPPORTED);
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
    if (job->paused || job->cancelled || !job->driver->complete) {
        error_set(errp, QERR_BLOCK_JOB_NOT_READY, job->bs->device_name);
        return;
    }

    job->driver->complete(job, errp);
}

void block_job_pause(BlockJob *job)
{
    job->paused = true;
}

bool block_job_is_paused(BlockJob *job)
{
    return job->paused;
}

void block_job_resume(BlockJob *job)
{
    job->paused = false;
    block_job_iostatus_reset(job);
    if (job->co && !job->busy) {
        qemu_coroutine_enter(job->co, NULL);
    }
}

void block_job_cancel(BlockJob *job)
{
    job->cancelled = true;
    block_job_resume(job);
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

struct BlockCancelData {
    BlockJob *job;
    BlockDriverCompletionFunc *cb;
    void *opaque;
    bool cancelled;
    int ret;
};

static void block_job_cancel_cb(void *opaque, int ret)
{
    struct BlockCancelData *data = opaque;

    data->cancelled = block_job_is_cancelled(data->job);
    data->ret = ret;
    data->cb(data->opaque, ret);
}

int block_job_cancel_sync(BlockJob *job)
{
    struct BlockCancelData data;
    BlockDriverState *bs = job->bs;

    assert(bs->job == job);

    /* Set up our own callback to store the result and chain to
     * the original callback.
     */
    data.job = job;
    data.cb = job->cb;
    data.opaque = job->opaque;
    data.ret = -EINPROGRESS;
    job->cb = block_job_cancel_cb;
    job->opaque = &data;
    block_job_cancel(job);
    while (data.ret == -EINPROGRESS) {
        qemu_aio_wait();
    }
    return (data.cancelled && data.ret == 0) ? -ECANCELED : data.ret;
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
        co_sleep_ns(type, ns);
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
    info->device    = g_strdup(bdrv_get_device_name(job->bs));
    info->len       = job->len;
    info->busy      = job->busy;
    info->paused    = job->paused;
    info->offset    = job->offset;
    info->speed     = job->speed;
    info->io_status = job->iostatus;
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
                                        bdrv_get_device_name(job->bs),
                                        job->len,
                                        job->offset,
                                        job->speed,
                                        &error_abort);
}

void block_job_event_completed(BlockJob *job, const char *msg)
{
    qapi_event_send_block_job_completed(job->driver->job_type,
                                        bdrv_get_device_name(job->bs),
                                        job->len,
                                        job->offset,
                                        job->speed,
                                        !!msg,
                                        msg,
                                        &error_abort);
}

void block_job_event_ready(BlockJob *job)
{
    qapi_event_send_block_job_ready(job->driver->job_type,
                                    bdrv_get_device_name(job->bs),
                                    job->len,
                                    job->offset,
                                    job->speed, &error_abort);
}

BlockErrorAction block_job_error_action(BlockJob *job, BlockDriverState *bs,
                                        BlockdevOnError on_err,
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
    qapi_event_send_block_job_error(bdrv_get_device_name(job->bs),
                                    is_read ? IO_OPERATION_TYPE_READ :
                                    IO_OPERATION_TYPE_WRITE,
                                    action, &error_abort);
    if (action == BLOCK_ERROR_ACTION_STOP) {
        block_job_pause(job);
        block_job_iostatus_set_err(job, error);
        if (bs != job->bs) {
            bdrv_iostatus_set_err(bs, error);
        }
    }
    return action;
}
