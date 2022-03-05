/*
 * Declarations for background jobs
 *
 * Copyright (c) 2011 IBM Corp.
 * Copyright (c) 2012, 2018 Red Hat, Inc.
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

#ifndef JOB_H
#define JOB_H

#include "qapi/qapi-types-job.h"
#include "qemu/queue.h"
#include "qemu/progress_meter.h"
#include "qemu/coroutine.h"
#include "block/aio.h"

typedef struct JobDriver JobDriver;
typedef struct JobTxn JobTxn;


/**
 * Long-running operation.
 */
typedef struct Job {
    /** The ID of the job. May be NULL for internal jobs. */
    char *id;

    /** The type of this job. */
    const JobDriver *driver;

    /** Reference count of the block job */
    int refcnt;

    /** Current state; See @JobStatus for details. */
    JobStatus status;

    /** AioContext to run the job coroutine in */
    AioContext *aio_context;

    /**
     * The coroutine that executes the job.  If not NULL, it is reentered when
     * busy is false and the job is cancelled.
     */
    Coroutine *co;

    /**
     * Timer that is used by @job_sleep_ns. Accessed under job_mutex (in
     * job.c).
     */
    QEMUTimer sleep_timer;

    /**
     * Counter for pause request. If non-zero, the block job is either paused,
     * or if busy == true will pause itself as soon as possible.
     */
    int pause_count;

    /**
     * Set to false by the job while the coroutine has yielded and may be
     * re-entered by job_enter(). There may still be I/O or event loop activity
     * pending. Accessed under block_job_mutex (in blockjob.c).
     *
     * When the job is deferred to the main loop, busy is true as long as the
     * bottom half is still pending.
     */
    bool busy;

    /**
     * Set to true by the job while it is in a quiescent state, where
     * no I/O or event loop activity is pending.
     */
    bool paused;

    /**
     * Set to true if the job is paused by user.  Can be unpaused with the
     * block-job-resume QMP command.
     */
    bool user_paused;

    /**
     * Set to true if the job should cancel itself.  The flag must
     * always be tested just before toggling the busy flag from false
     * to true.  After a job has been cancelled, it should only yield
     * if #aio_poll will ("sooner or later") reenter the coroutine.
     */
    bool cancelled;

    /**
     * Set to true if the job should abort immediately without waiting
     * for data to be in sync.
     */
    bool force_cancel;

    /** Set to true when the job has deferred work to the main loop. */
    bool deferred_to_main_loop;

    /** True if this job should automatically finalize itself */
    bool auto_finalize;

    /** True if this job should automatically dismiss itself */
    bool auto_dismiss;

    ProgressMeter progress;

    /**
     * Return code from @run and/or @prepare callback(s).
     * Not final until the job has reached the CONCLUDED status.
     * 0 on success, -errno on failure.
     */
    int ret;

    /**
     * Error object for a failed job.
     * If job->ret is nonzero and an error object was not set, it will be set
     * to strerror(-job->ret) during job_completed.
     */
    Error *err;

    /** The completion function that will be called when the job completes.  */
    BlockCompletionFunc *cb;

    /** The opaque value that is passed to the completion function.  */
    void *opaque;

    /** Notifiers called when a cancelled job is finalised */
    NotifierList on_finalize_cancelled;

    /** Notifiers called when a successfully completed job is finalised */
    NotifierList on_finalize_completed;

    /** Notifiers called when the job transitions to PENDING */
    NotifierList on_pending;

    /** Notifiers called when the job transitions to READY */
    NotifierList on_ready;

    /** Notifiers called when the job coroutine yields or terminates */
    NotifierList on_idle;

    /** Element of the list of jobs */
    QLIST_ENTRY(Job) job_list;

    /** Transaction this job is part of */
    JobTxn *txn;

    /** Element of the list of jobs in a job transaction */
    QLIST_ENTRY(Job) txn_list;
} Job;

/**
 * Callbacks and other information about a Job driver.
 */
struct JobDriver {

    /*
     * These fields are initialized when this object is created,
     * and are never changed afterwards
     */

    /** Derived Job struct size */
    size_t instance_size;

    /** Enum describing the operation */
    JobType job_type;

    /**
     * Mandatory: Entrypoint for the Coroutine.
     *
     * This callback will be invoked when moving from CREATED to RUNNING.
     *
     * If this callback returns nonzero, the job transaction it is part of is
     * aborted. If it returns zero, the job moves into the WAITING state. If it
     * is the last job to complete in its transaction, all jobs in the
     * transaction move from WAITING to PENDING.
     *
     * This callback must be run in the job's context.
     */
    int coroutine_fn (*run)(Job *job, Error **errp);

    /*
     * Functions run without regard to the BQL that may run in any
     * arbitrary thread. These functions do not need to be thread-safe
     * because the caller ensures that they are invoked from one
     * thread at time.
     */

    /**
     * If the callback is not NULL, it will be invoked when the job transitions
     * into the paused state.  Paused jobs must not perform any asynchronous
     * I/O or event loop activity.  This callback is used to quiesce jobs.
     */
    void coroutine_fn (*pause)(Job *job);

    /**
     * If the callback is not NULL, it will be invoked when the job transitions
     * out of the paused state.  Any asynchronous I/O or event loop activity
     * should be restarted from this callback.
     */
    void coroutine_fn (*resume)(Job *job);

    /*
     * Global state (GS) API. These functions run under the BQL.
     *
     * See include/block/block-global-state.h for more information about
     * the GS API.
     */

    /**
     * Called when the job is resumed by the user (i.e. user_paused becomes
     * false). .user_resume is called before .resume.
     */
    void (*user_resume)(Job *job);

    /**
     * Optional callback for job types whose completion must be triggered
     * manually.
     */
    void (*complete)(Job *job, Error **errp);

    /**
     * If the callback is not NULL, prepare will be invoked when all the jobs
     * belonging to the same transaction complete; or upon this job's completion
     * if it is not in a transaction.
     *
     * This callback will not be invoked if the job has already failed.
     * If it fails, abort and then clean will be called.
     */
    int (*prepare)(Job *job);

    /**
     * If the callback is not NULL, it will be invoked when all the jobs
     * belonging to the same transaction complete; or upon this job's
     * completion if it is not in a transaction. Skipped if NULL.
     *
     * All jobs will complete with a call to either .commit() or .abort() but
     * never both.
     */
    void (*commit)(Job *job);

    /**
     * If the callback is not NULL, it will be invoked when any job in the
     * same transaction fails; or upon this job's failure (due to error or
     * cancellation) if it is not in a transaction. Skipped if NULL.
     *
     * All jobs will complete with a call to either .commit() or .abort() but
     * never both.
     */
    void (*abort)(Job *job);

    /**
     * If the callback is not NULL, it will be invoked after a call to either
     * .commit() or .abort(). Regardless of which callback is invoked after
     * completion, .clean() will always be called, even if the job does not
     * belong to a transaction group.
     */
    void (*clean)(Job *job);

    /**
     * If the callback is not NULL, it will be invoked in job_cancel_async
     *
     * This function must return true if the job will be cancelled
     * immediately without any further I/O (mandatory if @force is
     * true), and false otherwise.  This lets the generic job layer
     * know whether a job has been truly (force-)cancelled, or whether
     * it is just in a special completion mode (like mirror after
     * READY).
     * (If the callback is NULL, the job is assumed to terminate
     * without I/O.)
     */
    bool (*cancel)(Job *job, bool force);


    /** Called when the job is freed */
    void (*free)(Job *job);
};

typedef enum JobCreateFlags {
    /* Default behavior */
    JOB_DEFAULT = 0x00,
    /* Job is not QMP-created and should not send QMP events */
    JOB_INTERNAL = 0x01,
    /* Job requires manual finalize step */
    JOB_MANUAL_FINALIZE = 0x02,
    /* Job requires manual dismiss step */
    JOB_MANUAL_DISMISS = 0x04,
} JobCreateFlags;

/**
 * Allocate and return a new job transaction. Jobs can be added to the
 * transaction using job_txn_add_job().
 *
 * The transaction is automatically freed when the last job completes or is
 * cancelled.
 *
 * All jobs in the transaction either complete successfully or fail/cancel as a
 * group.  Jobs wait for each other before completing.  Cancelling one job
 * cancels all jobs in the transaction.
 */
JobTxn *job_txn_new(void);

/**
 * Release a reference that was previously acquired with job_txn_add_job or
 * job_txn_new. If it's the last reference to the object, it will be freed.
 */
void job_txn_unref(JobTxn *txn);

/**
 * @txn: The transaction (may be NULL)
 * @job: Job to add to the transaction
 *
 * Add @job to the transaction.  The @job must not already be in a transaction.
 * The caller must call either job_txn_unref() or job_completed() to release
 * the reference that is automatically grabbed here.
 *
 * If @txn is NULL, the function does nothing.
 */
void job_txn_add_job(JobTxn *txn, Job *job);

/**
 * Create a new long-running job and return it.
 *
 * @job_id: The id of the newly-created job, or %NULL for internal jobs
 * @driver: The class object for the newly-created job.
 * @txn: The transaction this job belongs to, if any. %NULL otherwise.
 * @ctx: The AioContext to run the job coroutine in.
 * @flags: Creation flags for the job. See @JobCreateFlags.
 * @cb: Completion function for the job.
 * @opaque: Opaque pointer value passed to @cb.
 * @errp: Error object.
 */
void *job_create(const char *job_id, const JobDriver *driver, JobTxn *txn,
                 AioContext *ctx, int flags, BlockCompletionFunc *cb,
                 void *opaque, Error **errp);

/**
 * Add a reference to Job refcnt, it will be decreased with job_unref, and then
 * be freed if it comes to be the last reference.
 */
void job_ref(Job *job);

/**
 * Release a reference that was previously acquired with job_ref() or
 * job_create(). If it's the last reference to the object, it will be freed.
 */
void job_unref(Job *job);

/**
 * @job: The job that has made progress
 * @done: How much progress the job made since the last call
 *
 * Updates the progress counter of the job.
 */
void job_progress_update(Job *job, uint64_t done);

/**
 * @job: The job whose expected progress end value is set
 * @remaining: Missing progress (on top of the current progress counter value)
 *             until the new expected end value is reached
 *
 * Sets the expected end value of the progress counter of a job so that a
 * completion percentage can be calculated when the progress is updated.
 */
void job_progress_set_remaining(Job *job, uint64_t remaining);

/**
 * @job: The job whose expected progress end value is updated
 * @delta: Value which is to be added to the current expected end
 *         value
 *
 * Increases the expected end value of the progress counter of a job.
 * This is useful for parenthesis operations: If a job has to
 * conditionally perform a high-priority operation as part of its
 * progress, it calls this function with the expected operation's
 * length before, and job_progress_update() afterwards.
 * (So the operation acts as a parenthesis in regards to the main job
 * operation running in background.)
 */
void job_progress_increase_remaining(Job *job, uint64_t delta);

/** To be called when a cancelled job is finalised. */
void job_event_cancelled(Job *job);

/** To be called when a successfully completed job is finalised. */
void job_event_completed(Job *job);

/**
 * Conditionally enter the job coroutine if the job is ready to run, not
 * already busy and fn() returns true. fn() is called while under the job_lock
 * critical section.
 */
void job_enter_cond(Job *job, bool(*fn)(Job *job));

/**
 * @job: A job that has not yet been started.
 *
 * Begins execution of a job.
 * Takes ownership of one reference to the job object.
 */
void job_start(Job *job);

/**
 * @job: The job to enter.
 *
 * Continue the specified job by entering the coroutine.
 */
void job_enter(Job *job);

/**
 * @job: The job that is ready to pause.
 *
 * Pause now if job_pause() has been called. Jobs that perform lots of I/O
 * must call this between requests so that the job can be paused.
 */
void coroutine_fn job_pause_point(Job *job);

/**
 * @job: The job that calls the function.
 *
 * Yield the job coroutine.
 */
void job_yield(Job *job);

/**
 * @job: The job that calls the function.
 * @ns: How many nanoseconds to stop for.
 *
 * Put the job to sleep (assuming that it wasn't canceled) for @ns
 * %QEMU_CLOCK_REALTIME nanoseconds.  Canceling the job will immediately
 * interrupt the wait.
 */
void coroutine_fn job_sleep_ns(Job *job, int64_t ns);


/** Returns the JobType of a given Job. */
JobType job_type(const Job *job);

/** Returns the enum string for the JobType of a given Job. */
const char *job_type_str(const Job *job);

/** Returns true if the job should not be visible to the management layer. */
bool job_is_internal(Job *job);

/** Returns whether the job is being cancelled. */
bool job_is_cancelled(Job *job);

/**
 * Returns whether the job is scheduled for cancellation (at an
 * indefinite point).
 */
bool job_cancel_requested(Job *job);

/** Returns whether the job is in a completed state. */
bool job_is_completed(Job *job);

/** Returns whether the job is ready to be completed. */
bool job_is_ready(Job *job);

/**
 * Request @job to pause at the next pause point. Must be paired with
 * job_resume(). If the job is supposed to be resumed by user action, call
 * job_user_pause() instead.
 */
void job_pause(Job *job);

/** Resumes a @job paused with job_pause. */
void job_resume(Job *job);

/**
 * Asynchronously pause the specified @job.
 * Do not allow a resume until a matching call to job_user_resume.
 */
void job_user_pause(Job *job, Error **errp);

/** Returns true if the job is user-paused. */
bool job_user_paused(Job *job);

/**
 * Resume the specified @job.
 * Must be paired with a preceding job_user_pause.
 */
void job_user_resume(Job *job, Error **errp);

/**
 * Get the next element from the list of block jobs after @job, or the
 * first one if @job is %NULL.
 *
 * Returns the requested job, or %NULL if there are no more jobs left.
 */
Job *job_next(Job *job);

/**
 * Get the job identified by @id (which must not be %NULL).
 *
 * Returns the requested job, or %NULL if it doesn't exist.
 */
Job *job_get(const char *id);

/**
 * Check whether the verb @verb can be applied to @job in its current state.
 * Returns 0 if the verb can be applied; otherwise errp is set and -EPERM
 * returned.
 */
int job_apply_verb(Job *job, JobVerb verb, Error **errp);

/** The @job could not be started, free it. */
void job_early_fail(Job *job);

/** Moves the @job from RUNNING to READY */
void job_transition_to_ready(Job *job);

/** Asynchronously complete the specified @job. */
void job_complete(Job *job, Error **errp);

/**
 * Asynchronously cancel the specified @job. If @force is true, the job should
 * be cancelled immediately without waiting for a consistent state.
 */
void job_cancel(Job *job, bool force);

/**
 * Cancels the specified job like job_cancel(), but may refuse to do so if the
 * operation isn't meaningful in the current state of the job.
 */
void job_user_cancel(Job *job, bool force, Error **errp);

/**
 * Synchronously cancel the @job.  The completion callback is called
 * before the function returns.  If @force is false, the job may
 * actually complete instead of canceling itself; the circumstances
 * under which this happens depend on the kind of job that is active.
 *
 * Returns the return value from the job if the job actually completed
 * during the call, or -ECANCELED if it was canceled.
 *
 * Callers must hold the AioContext lock of job->aio_context.
 */
int job_cancel_sync(Job *job, bool force);

/** Synchronously force-cancels all jobs using job_cancel_sync(). */
void job_cancel_sync_all(void);

/**
 * @job: The job to be completed.
 * @errp: Error object which may be set by job_complete(); this is not
 *        necessarily set on every error, the job return value has to be
 *        checked as well.
 *
 * Synchronously complete the job.  The completion callback is called before the
 * function returns, unless it is NULL (which is permissible when using this
 * function).
 *
 * Returns the return value from the job.
 *
 * Callers must hold the AioContext lock of job->aio_context.
 */
int job_complete_sync(Job *job, Error **errp);

/**
 * For a @job that has finished its work and is pending awaiting explicit
 * acknowledgement to commit its work, this will commit that work.
 *
 * FIXME: Make the below statement universally true:
 * For jobs that support the manual workflow mode, all graph changes that occur
 * as a result will occur after this command and before a successful reply.
 */
void job_finalize(Job *job, Error **errp);

/**
 * Remove the concluded @job from the query list and resets the passed pointer
 * to %NULL. Returns an error if the job is not actually concluded.
 */
void job_dismiss(Job **job, Error **errp);

/**
 * Synchronously finishes the given @job. If @finish is given, it is called to
 * trigger completion or cancellation of the job.
 *
 * Returns 0 if the job is successfully completed, -ECANCELED if the job was
 * cancelled before completing, and -errno in other error cases.
 *
 * Callers must hold the AioContext lock of job->aio_context.
 */
int job_finish_sync(Job *job, void (*finish)(Job *, Error **errp), Error **errp);

#endif
