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

    /** Current state; See @JobStatus for details. */
    JobStatus status;

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
};


/**
 * Create a new long-running job and return it.
 *
 * @job_id: The id of the newly-created job, or %NULL for internal jobs
 * @driver: The class object for the newly-created job.
 * @errp: Error object.
 */
void *job_create(const char *job_id, const JobDriver *driver, Error **errp);

/** Frees the @job object. */
void job_delete(Job *job);

/** Returns the JobType of a given Job. */
JobType job_type(const Job *job);

/** Returns the enum string for the JobType of a given Job. */
const char *job_type_str(const Job *job);

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

/* TODO To be removed from the public interface */
void job_state_transition(Job *job, JobStatus s1);

#endif
