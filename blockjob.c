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
#include "monitor/monitor.h"
#include "block/block.h"
#include "block/blockjob.h"
#include "block/block_int.h"
#include "qapi/qmp/qjson.h"
#include "block/coroutine.h"
#include "qmp-commands.h"
#include "qemu/timer.h"

void *block_job_create(const BlockJobDriver *driver, BlockDriverState *bs,
                       int64_t speed, BlockDriverCompletionFunc *cb,
                       void *opaque, Error **errp)
{
    BlockJob *job;

    if (bs->job || bdrv_in_use(bs)) {
        error_set(errp, QERR_DEVICE_IN_USE, bdrv_get_device_name(bs));
        return NULL;
    }
    bdrv_ref(bs);
    bdrv_set_in_use(bs, 1);

    job = g_malloc0(driver->instance_size);
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
            g_free(job);
            bdrv_set_in_use(bs, 0);
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
    g_free(job);
    bdrv_set_in_use(bs, 0);
}

void block_job_set_speed(BlockJob *job, int64_t speed, Error **errp)
{
    Error *local_err = NULL;

    if (!job->driver->set_speed) {
        error_set(errp, QERR_NOT_SUPPORTED);
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


QObject *qobject_from_block_job(BlockJob *job)
{
    return qobject_from_jsonf("{ 'type': %s,"
                              "'device': %s,"
                              "'len': %" PRId64 ","
                              "'offset': %" PRId64 ","
                              "'speed': %" PRId64 " }",
                              BlockJobType_lookup[job->driver->job_type],
                              bdrv_get_device_name(job->bs),
                              job->len,
                              job->offset,
                              job->speed);
}

void block_job_ready(BlockJob *job)
{
    QObject *data = qobject_from_block_job(job);
    monitor_protocol_event(QEVENT_BLOCK_JOB_READY, data);
    qobject_decref(data);
}

BlockErrorAction block_job_error_action(BlockJob *job, BlockDriverState *bs,
                                        BlockdevOnError on_err,
                                        int is_read, int error)
{
    BlockErrorAction action;

    switch (on_err) {
    case BLOCKDEV_ON_ERROR_ENOSPC:
        action = (error == ENOSPC) ? BDRV_ACTION_STOP : BDRV_ACTION_REPORT;
        break;
    case BLOCKDEV_ON_ERROR_STOP:
        action = BDRV_ACTION_STOP;
        break;
    case BLOCKDEV_ON_ERROR_REPORT:
        action = BDRV_ACTION_REPORT;
        break;
    case BLOCKDEV_ON_ERROR_IGNORE:
        action = BDRV_ACTION_IGNORE;
        break;
    default:
        abort();
    }
    bdrv_emit_qmp_error_event(job->bs, QEVENT_BLOCK_JOB_ERROR, action, is_read);
    if (action == BDRV_ACTION_STOP) {
        block_job_pause(job);
        block_job_iostatus_set_err(job, error);
        if (bs != job->bs) {
            bdrv_iostatus_set_err(bs, error);
        }
    }
    return action;
}
