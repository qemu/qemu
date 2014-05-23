/*
 * Declarations for long-running block device operations
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
#ifndef BLOCKJOB_H
#define BLOCKJOB_H 1

#include "block/block.h"

/**
 * BlockJobDriver:
 *
 * A class type for block job driver.
 */
typedef struct BlockJobDriver {
    /** Derived BlockJob struct size */
    size_t instance_size;

    /** String describing the operation, part of query-block-jobs QMP API */
    BlockJobType job_type;

    /** Optional callback for job types that support setting a speed limit */
    void (*set_speed)(BlockJob *job, int64_t speed, Error **errp);

    /** Optional callback for job types that need to forward I/O status reset */
    void (*iostatus_reset)(BlockJob *job);

    /**
     * Optional callback for job types whose completion must be triggered
     * manually.
     */
    void (*complete)(BlockJob *job, Error **errp);
} BlockJobDriver;

/**
 * BlockJob:
 *
 * Long-running operation on a BlockDriverState.
 */
struct BlockJob {
    /** The job type, including the job vtable.  */
    const BlockJobDriver *driver;

    /** The block device on which the job is operating.  */
    BlockDriverState *bs;

    /**
     * The coroutine that executes the job.  If not NULL, it is
     * reentered when busy is false and the job is cancelled.
     */
    Coroutine *co;

    /**
     * Set to true if the job should cancel itself.  The flag must
     * always be tested just before toggling the busy flag from false
     * to true.  After a job has been cancelled, it should only yield
     * if #qemu_aio_wait will ("sooner or later") reenter the coroutine.
     */
    bool cancelled;

    /**
     * Set to true if the job is either paused, or will pause itself
     * as soon as possible (if busy == true).
     */
    bool paused;

    /**
     * Set to false by the job while it is in a quiescent state, where
     * no I/O is pending and the job has yielded on any condition
     * that is not detected by #qemu_aio_wait, such as a timer.
     */
    bool busy;

    /** Status that is published by the query-block-jobs QMP API */
    BlockDeviceIoStatus iostatus;

    /** Offset that is published by the query-block-jobs QMP API */
    int64_t offset;

    /** Length that is published by the query-block-jobs QMP API */
    int64_t len;

    /** Speed that was set with @block_job_set_speed.  */
    int64_t speed;

    /** The completion function that will be called when the job completes.  */
    BlockDriverCompletionFunc *cb;

    /** Block other operations when block job is running */
    Error *blocker;

    /** The opaque value that is passed to the completion function.  */
    void *opaque;
};

/**
 * block_job_create:
 * @job_type: The class object for the newly-created job.
 * @bs: The block
 * @speed: The maximum speed, in bytes per second, or 0 for unlimited.
 * @cb: Completion function for the job.
 * @opaque: Opaque pointer value passed to @cb.
 * @errp: Error object.
 *
 * Create a new long-running block device job and return it.  The job
 * will call @cb asynchronously when the job completes.  Note that
 * @bs may have been closed at the time the @cb it is called.  If
 * this is the case, the job may be reported as either cancelled or
 * completed.
 *
 * This function is not part of the public job interface; it should be
 * called from a wrapper that is specific to the job type.
 */
void *block_job_create(const BlockJobDriver *driver, BlockDriverState *bs,
                       int64_t speed, BlockDriverCompletionFunc *cb,
                       void *opaque, Error **errp);

/**
 * block_job_sleep_ns:
 * @job: The job that calls the function.
 * @clock: The clock to sleep on.
 * @ns: How many nanoseconds to stop for.
 *
 * Put the job to sleep (assuming that it wasn't canceled) for @ns
 * nanoseconds.  Canceling the job will interrupt the wait immediately.
 */
void block_job_sleep_ns(BlockJob *job, QEMUClockType type, int64_t ns);

/**
 * block_job_completed:
 * @job: The job being completed.
 * @ret: The status code.
 *
 * Call the completion function that was registered at creation time, and
 * free @job.
 */
void block_job_completed(BlockJob *job, int ret);

/**
 * block_job_set_speed:
 * @job: The job to set the speed for.
 * @speed: The new value
 * @errp: Error object.
 *
 * Set a rate-limiting parameter for the job; the actual meaning may
 * vary depending on the job type.
 */
void block_job_set_speed(BlockJob *job, int64_t speed, Error **errp);

/**
 * block_job_cancel:
 * @job: The job to be canceled.
 *
 * Asynchronously cancel the specified job.
 */
void block_job_cancel(BlockJob *job);

/**
 * block_job_complete:
 * @job: The job to be completed.
 * @errp: Error object.
 *
 * Asynchronously complete the specified job.
 */
void block_job_complete(BlockJob *job, Error **errp);

/**
 * block_job_is_cancelled:
 * @job: The job being queried.
 *
 * Returns whether the job is scheduled for cancellation.
 */
bool block_job_is_cancelled(BlockJob *job);

/**
 * block_job_query:
 * @job: The job to get information about.
 *
 * Return information about a job.
 */
BlockJobInfo *block_job_query(BlockJob *job);

/**
 * block_job_pause:
 * @job: The job to be paused.
 *
 * Asynchronously pause the specified job.
 */
void block_job_pause(BlockJob *job);

/**
 * block_job_resume:
 * @job: The job to be resumed.
 *
 * Resume the specified job.
 */
void block_job_resume(BlockJob *job);

/**
 * qobject_from_block_job:
 * @job: The job whose information is requested.
 *
 * Return a QDict corresponding to @job's query-block-jobs entry.
 */
QObject *qobject_from_block_job(BlockJob *job);

/**
 * block_job_ready:
 * @job: The job which is now ready to complete.
 *
 * Send a BLOCK_JOB_READY event for the specified job.
 */
void block_job_ready(BlockJob *job);

/**
 * block_job_is_paused:
 * @job: The job being queried.
 *
 * Returns whether the job is currently paused, or will pause
 * as soon as it reaches a sleeping point.
 */
bool block_job_is_paused(BlockJob *job);

/**
 * block_job_cancel_sync:
 * @job: The job to be canceled.
 *
 * Synchronously cancel the job.  The completion callback is called
 * before the function returns.  The job may actually complete
 * instead of canceling itself; the circumstances under which this
 * happens depend on the kind of job that is active.
 *
 * Returns the return value from the job if the job actually completed
 * during the call, or -ECANCELED if it was canceled.
 */
int block_job_cancel_sync(BlockJob *job);

/**
 * block_job_iostatus_reset:
 * @job: The job whose I/O status should be reset.
 *
 * Reset I/O status on @job and on BlockDriverState objects it uses,
 * other than job->bs.
 */
void block_job_iostatus_reset(BlockJob *job);

/**
 * block_job_error_action:
 * @job: The job to signal an error for.
 * @bs: The block device on which to set an I/O error.
 * @on_err: The error action setting.
 * @is_read: Whether the operation was a read.
 * @error: The error that was reported.
 *
 * Report an I/O error for a block job and possibly stop the VM.  Return the
 * action that was selected based on @on_err and @error.
 */
BlockErrorAction block_job_error_action(BlockJob *job, BlockDriverState *bs,
                                        BlockdevOnError on_err,
                                        int is_read, int error);
#endif
