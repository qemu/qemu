/*
 * QMP interface for background jobs
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

#include "qemu/osdep.h"
#include "qemu/job.h"
#include "qapi/qapi-commands-job.h"
#include "qapi/error.h"
#include "trace/trace-root.h"

/*
 * Get a job using its ID. Called with job_mutex held.
 */
static Job *find_job_locked(const char *id, Error **errp)
{
    Job *job;

    job = job_get_locked(id);
    if (!job) {
        error_setg(errp, "Job not found");
        return NULL;
    }

    return job;
}

void qmp_job_cancel(const char *id, Error **errp)
{
    Job *job;

    JOB_LOCK_GUARD();
    job = find_job_locked(id, errp);

    if (!job) {
        return;
    }

    trace_qmp_job_cancel(job);
    job_user_cancel_locked(job, true, errp);
}

void qmp_job_pause(const char *id, Error **errp)
{
    Job *job;

    JOB_LOCK_GUARD();
    job = find_job_locked(id, errp);

    if (!job) {
        return;
    }

    trace_qmp_job_pause(job);
    job_user_pause_locked(job, errp);
}

void qmp_job_resume(const char *id, Error **errp)
{
    Job *job;

    JOB_LOCK_GUARD();
    job = find_job_locked(id, errp);

    if (!job) {
        return;
    }

    trace_qmp_job_resume(job);
    job_user_resume_locked(job, errp);
}

void qmp_job_complete(const char *id, Error **errp)
{
    Job *job;

    JOB_LOCK_GUARD();
    job = find_job_locked(id, errp);

    if (!job) {
        return;
    }

    trace_qmp_job_complete(job);
    job_complete_locked(job, errp);
}

void qmp_job_finalize(const char *id, Error **errp)
{
    Job *job;

    JOB_LOCK_GUARD();
    job = find_job_locked(id, errp);

    if (!job) {
        return;
    }

    trace_qmp_job_finalize(job);
    job_ref_locked(job);
    job_finalize_locked(job, errp);

    job_unref_locked(job);
}

void qmp_job_dismiss(const char *id, Error **errp)
{
    Job *job;

    JOB_LOCK_GUARD();
    job = find_job_locked(id, errp);

    if (!job) {
        return;
    }

    trace_qmp_job_dismiss(job);
    job_dismiss_locked(&job, errp);
}

/* Called with job_mutex held. */
static JobInfo *job_query_single_locked(Job *job, Error **errp)
{
    JobInfo *info;
    uint64_t progress_current;
    uint64_t progress_total;

    assert(!job_is_internal(job));
    progress_get_snapshot(&job->progress, &progress_current,
                          &progress_total);

    info = g_new(JobInfo, 1);
    *info = (JobInfo) {
        .id                 = g_strdup(job->id),
        .type               = job_type(job),
        .status             = job->status,
        .current_progress   = progress_current,
        .total_progress     = progress_total,
        .error              = job->err ?
                              g_strdup(error_get_pretty(job->err)) : NULL,
    };

    return info;
}

JobInfoList *qmp_query_jobs(Error **errp)
{
    JobInfoList *head = NULL, **tail = &head;
    Job *job;

    JOB_LOCK_GUARD();

    for (job = job_next_locked(NULL); job; job = job_next_locked(job)) {
        JobInfo *value;

        if (job_is_internal(job)) {
            continue;
        }
        value = job_query_single_locked(job, errp);
        if (!value) {
            qapi_free_JobInfoList(head);
            return NULL;
        }
        QAPI_LIST_APPEND(tail, value);
    }

    return head;
}
