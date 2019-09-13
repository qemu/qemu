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

#ifndef BLOCKJOB_INT_H
#define BLOCKJOB_INT_H

#include "block/blockjob.h"
#include "block/block.h"

/**
 * BlockJobDriver:
 *
 * A class type for block job driver.
 */
struct BlockJobDriver {
    /** Generic JobDriver callbacks and settings */
    JobDriver job_driver;

    /*
     * Returns whether the job has pending requests for the child or will
     * submit new requests before the next pause point. This callback is polled
     * in the context of draining a job node after requesting that the job be
     * paused, until all activity on the child has stopped.
     */
    bool (*drained_poll)(BlockJob *job);

    /*
     * If the callback is not NULL, it will be invoked before the job is
     * resumed in a new AioContext.  This is the place to move any resources
     * besides job->blk to the new AioContext.
     */
    void (*attached_aio_context)(BlockJob *job, AioContext *new_context);
};

/**
 * block_job_create:
 * @job_id: The id of the newly-created job, or %NULL to have one
 * generated automatically.
 * @driver: The class object for the newly-created job.
 * @txn: The transaction this job belongs to, if any. %NULL otherwise.
 * @bs: The block
 * @perm, @shared_perm: Permissions to request for @bs
 * @speed: The maximum speed, in bytes per second, or 0 for unlimited.
 * @flags: Creation flags for the Block Job. See @JobCreateFlags.
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
void *block_job_create(const char *job_id, const BlockJobDriver *driver,
                       JobTxn *txn, BlockDriverState *bs, uint64_t perm,
                       uint64_t shared_perm, int64_t speed, int flags,
                       BlockCompletionFunc *cb, void *opaque, Error **errp);

/**
 * block_job_free:
 * Callback to be used for JobDriver.free in all block jobs. Frees block job
 * specific resources in @job.
 */
void block_job_free(Job *job);

/**
 * block_job_user_resume:
 * Callback to be used for JobDriver.user_resume in all block jobs. Resets the
 * iostatus when the user resumes @job.
 */
void block_job_user_resume(Job *job);

/**
 * block_job_ratelimit_get_delay:
 *
 * Calculate and return delay for the next request in ns. See the documentation
 * of ratelimit_calculate_delay() for details.
 */
int64_t block_job_ratelimit_get_delay(BlockJob *job, uint64_t n);

/**
 * block_job_error_action:
 * @job: The job to signal an error for.
 * @on_err: The error action setting.
 * @is_read: Whether the operation was a read.
 * @error: The error that was reported.
 *
 * Report an I/O error for a block job and possibly stop the VM.  Return the
 * action that was selected based on @on_err and @error.
 */
BlockErrorAction block_job_error_action(BlockJob *job, BlockdevOnError on_err,
                                        int is_read, int error);

#endif
