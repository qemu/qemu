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

/** Returns the JobType of a given Job. */
JobType job_type(const Job *job);

/** Returns the enum string for the JobType of a given Job. */
const char *job_type_str(const Job *job);

/** Returns whether the job is scheduled for cancellation. */
bool job_is_cancelled(Job *job);

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

#endif
