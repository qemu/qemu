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
#define BLOCKJOB_H

#include "block/block.h"

typedef struct BlockJobDriver BlockJobDriver;
typedef struct BlockJobTxn BlockJobTxn;

/**
 * BlockJob:
 *
 * Long-running operation on a BlockDriverState.
 */
typedef struct BlockJob {
    /** The job type, including the job vtable.  */
    const BlockJobDriver *driver;

    /** The block device on which the job is operating.  */
    BlockBackend *blk;

    /**
     * The ID of the block job. May be NULL for internal jobs.
     */
    char *id;

    /**
     * The coroutine that executes the job.  If not NULL, it is
     * reentered when busy is false and the job is cancelled.
     */
    Coroutine *co;

    /**
     * Set to true if the job should cancel itself.  The flag must
     * always be tested just before toggling the busy flag from false
     * to true.  After a job has been cancelled, it should only yield
     * if #aio_poll will ("sooner or later") reenter the coroutine.
     */
    bool cancelled;

    /**
     * Counter for pause request. If non-zero, the block job is either paused,
     * or if busy == true will pause itself as soon as possible.
     */
    int pause_count;

    /**
     * Set to true if the job is paused by user.  Can be unpaused with the
     * block-job-resume QMP command.
     */
    bool user_paused;

    /**
     * Set to false by the job while the coroutine has yielded and may be
     * re-entered by block_job_enter().  There may still be I/O or event loop
     * activity pending.
     */
    bool busy;

    /**
     * Set to true by the job while it is in a quiescent state, where
     * no I/O or event loop activity is pending.
     */
    bool paused;

    /**
     * Set to true when the job is ready to be completed.
     */
    bool ready;

    /**
     * Set to true when the job has deferred work to the main loop.
     */
    bool deferred_to_main_loop;

    /** Element of the list of block jobs */
    QLIST_ENTRY(BlockJob) job_list;

    /** Status that is published by the query-block-jobs QMP API */
    BlockDeviceIoStatus iostatus;

    /** Offset that is published by the query-block-jobs QMP API */
    int64_t offset;

    /** Length that is published by the query-block-jobs QMP API */
    int64_t len;

    /** Speed that was set with @block_job_set_speed.  */
    int64_t speed;

    /** The completion function that will be called when the job completes.  */
    BlockCompletionFunc *cb;

    /** Block other operations when block job is running */
    Error *blocker;

    /** BlockDriverStates that are involved in this block job */
    GSList *nodes;

    /** The opaque value that is passed to the completion function.  */
    void *opaque;

    /** Reference count of the block job */
    int refcnt;

    /* True if this job has reported completion by calling block_job_completed.
     */
    bool completed;

    /* ret code passed to block_job_completed.
     */
    int ret;

    /** Non-NULL if this job is part of a transaction */
    BlockJobTxn *txn;
    QLIST_ENTRY(BlockJob) txn_list;
} BlockJob;

typedef enum BlockJobCreateFlags {
    BLOCK_JOB_DEFAULT = 0x00,
    BLOCK_JOB_INTERNAL = 0x01,
} BlockJobCreateFlags;

/**
 * block_job_next:
 * @job: A block job, or %NULL.
 *
 * Get the next element from the list of block jobs after @job, or the
 * first one if @job is %NULL.
 *
 * Returns the requested job, or %NULL if there are no more jobs left.
 */
BlockJob *block_job_next(BlockJob *job);

/**
 * block_job_get:
 * @id: The id of the block job.
 *
 * Get the block job identified by @id (which must not be %NULL).
 *
 * Returns the requested job, or %NULL if it doesn't exist.
 */
BlockJob *block_job_get(const char *id);

/**
 * block_job_add_bdrv:
 * @job: A block job
 * @name: The name to assign to the new BdrvChild
 * @bs: A BlockDriverState that is involved in @job
 * @perm, @shared_perm: Permissions to request on the node
 *
 * Add @bs to the list of BlockDriverState that are involved in
 * @job. This means that all operations will be blocked on @bs while
 * @job exists.
 */
int block_job_add_bdrv(BlockJob *job, const char *name, BlockDriverState *bs,
                       uint64_t perm, uint64_t shared_perm, Error **errp);

/**
 * block_job_remove_all_bdrv:
 * @job: The block job
 *
 * Remove all BlockDriverStates from the list of nodes that are involved in the
 * job. This removes the blockers added with block_job_add_bdrv().
 */
void block_job_remove_all_bdrv(BlockJob *job);

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
 * block_job_start:
 * @job: A job that has not yet been started.
 *
 * Begins execution of a block job.
 * Takes ownership of one reference to the job object.
 */
void block_job_start(BlockJob *job);

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
 * block_job_query:
 * @job: The job to get information about.
 *
 * Return information about a job.
 */
BlockJobInfo *block_job_query(BlockJob *job, Error **errp);

/**
 * block_job_pause:
 * @job: The job to be paused.
 *
 * Asynchronously pause the specified job.
 */
void block_job_pause(BlockJob *job);

/**
 * block_job_user_pause:
 * @job: The job to be paused.
 *
 * Asynchronously pause the specified job.
 * Do not allow a resume until a matching call to block_job_user_resume.
 */
void block_job_user_pause(BlockJob *job);

/**
 * block_job_paused:
 * @job: The job to query.
 *
 * Returns true if the job is user-paused.
 */
bool block_job_user_paused(BlockJob *job);

/**
 * block_job_resume:
 * @job: The job to be resumed.
 *
 * Resume the specified job.  Must be paired with a preceding block_job_pause.
 */
void block_job_resume(BlockJob *job);

/**
 * block_job_user_resume:
 * @job: The job to be resumed.
 *
 * Resume the specified job.
 * Must be paired with a preceding block_job_user_pause.
 */
void block_job_user_resume(BlockJob *job);

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
 * block_job_cancel_sync_all:
 *
 * Synchronously cancels all jobs using block_job_cancel_sync().
 */
void block_job_cancel_sync_all(void);

/**
 * block_job_complete_sync:
 * @job: The job to be completed.
 * @errp: Error object which may be set by block_job_complete(); this is not
 *        necessarily set on every error, the job return value has to be
 *        checked as well.
 *
 * Synchronously complete the job.  The completion callback is called before the
 * function returns, unless it is NULL (which is permissible when using this
 * function).
 *
 * Returns the return value from the job.
 */
int block_job_complete_sync(BlockJob *job, Error **errp);

/**
 * block_job_iostatus_reset:
 * @job: The job whose I/O status should be reset.
 *
 * Reset I/O status on @job and on BlockDriverState objects it uses,
 * other than job->blk.
 */
void block_job_iostatus_reset(BlockJob *job);

/**
 * block_job_txn_new:
 *
 * Allocate and return a new block job transaction.  Jobs can be added to the
 * transaction using block_job_txn_add_job().
 *
 * The transaction is automatically freed when the last job completes or is
 * cancelled.
 *
 * All jobs in the transaction either complete successfully or fail/cancel as a
 * group.  Jobs wait for each other before completing.  Cancelling one job
 * cancels all jobs in the transaction.
 */
BlockJobTxn *block_job_txn_new(void);

/**
 * block_job_txn_unref:
 *
 * Release a reference that was previously acquired with block_job_txn_add_job
 * or block_job_txn_new. If it's the last reference to the object, it will be
 * freed.
 */
void block_job_txn_unref(BlockJobTxn *txn);

/**
 * block_job_txn_add_job:
 * @txn: The transaction (may be NULL)
 * @job: Job to add to the transaction
 *
 * Add @job to the transaction.  The @job must not already be in a transaction.
 * The caller must call either block_job_txn_unref() or block_job_completed()
 * to release the reference that is automatically grabbed here.
 */
void block_job_txn_add_job(BlockJobTxn *txn, BlockJob *job);

/**
 * block_job_is_internal:
 * @job: The job to determine if it is user-visible or not.
 *
 * Returns true if the job should not be visible to the management layer.
 */
bool block_job_is_internal(BlockJob *job);

#endif
