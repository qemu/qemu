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

#include "qapi/qapi-types-block-core.h"
#include "qemu/queue.h"
#include "qemu/coroutine.h"

typedef struct JobDriver JobDriver;

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
     * re-entered by block_job_enter().  There may still be I/O or event loop
     * activity pending.  Accessed under block_job_mutex (in blockjob.c).
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

    /** Set to true when the job has deferred work to the main loop. */
    bool deferred_to_main_loop;

    /** Element of the list of jobs */
    QLIST_ENTRY(Job) job_list;
} Job;

/**
 * Callbacks and other information about a Job driver.
 */
struct JobDriver {
    /** Derived Job struct size */
    size_t instance_size;

    /** Enum describing the operation */
    JobType job_type;

    /** Mandatory: Entrypoint for the Coroutine. */
    CoroutineEntry *start;

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

    /**
     * Called when the job is resumed by the user (i.e. user_paused becomes
     * false). .user_resume is called before .resume.
     */
    void (*user_resume)(Job *job);

    /** Called when the job is freed */
    void (*free)(Job *job);
};


/**
 * Create a new long-running job and return it.
 *
 * @job_id: The id of the newly-created job, or %NULL for internal jobs
 * @driver: The class object for the newly-created job.
 * @ctx: The AioContext to run the job coroutine in.
 * @errp: Error object.
 */
void *job_create(const char *job_id, const JobDriver *driver, AioContext *ctx,
                 Error **errp);

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

/** Returns whether the job is scheduled for cancellation. */
bool job_is_cancelled(Job *job);

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

typedef void JobDeferToMainLoopFn(Job *job, void *opaque);

/**
 * @job: The job
 * @fn: The function to run in the main loop
 * @opaque: The opaque value that is passed to @fn
 *
 * This function must be called by the main job coroutine just before it
 * returns.  @fn is executed in the main loop with the job AioContext acquired.
 *
 * Block jobs must call bdrv_unref(), bdrv_close(), and anything that uses
 * bdrv_drain_all() in the main loop.
 *
 * The @job AioContext is held while @fn executes.
 */
void job_defer_to_main_loop(Job *job, JobDeferToMainLoopFn *fn, void *opaque);

/* TODO To be removed from the public interface */
void job_state_transition(Job *job, JobStatus s1);
void coroutine_fn job_do_yield(Job *job, uint64_t ns);
bool job_should_pause(Job *job);
bool job_started(Job *job);

#endif
