/*
 * Background jobs (long-running operations)
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
#include "qapi/error.h"
#include "qemu/job.h"
#include "qemu/id.h"
#include "qemu/main-loop.h"
#include "block/aio-wait.h"
#include "trace-root.h"
#include "qapi/qapi-events-job.h"

static QLIST_HEAD(, Job) jobs = QLIST_HEAD_INITIALIZER(jobs);

/* Job State Transition Table */
bool JobSTT[JOB_STATUS__MAX][JOB_STATUS__MAX] = {
                                    /* U, C, R, P, Y, S, W, D, X, E, N */
    /* U: */ [JOB_STATUS_UNDEFINED] = {0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* C: */ [JOB_STATUS_CREATED]   = {0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 1},
    /* R: */ [JOB_STATUS_RUNNING]   = {0, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0},
    /* P: */ [JOB_STATUS_PAUSED]    = {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    /* Y: */ [JOB_STATUS_READY]     = {0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 0},
    /* S: */ [JOB_STATUS_STANDBY]   = {0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    /* W: */ [JOB_STATUS_WAITING]   = {0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0},
    /* D: */ [JOB_STATUS_PENDING]   = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0},
    /* X: */ [JOB_STATUS_ABORTING]  = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0},
    /* E: */ [JOB_STATUS_CONCLUDED] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    /* N: */ [JOB_STATUS_NULL]      = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
};

bool JobVerbTable[JOB_VERB__MAX][JOB_STATUS__MAX] = {
                                    /* U, C, R, P, Y, S, W, D, X, E, N */
    [JOB_VERB_CANCEL]               = {0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0},
    [JOB_VERB_PAUSE]                = {0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0},
    [JOB_VERB_RESUME]               = {0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0},
    [JOB_VERB_SET_SPEED]            = {0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0},
    [JOB_VERB_COMPLETE]             = {0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    [JOB_VERB_FINALIZE]             = {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0},
    [JOB_VERB_DISMISS]              = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0},
};

/* Transactional group of jobs */
struct JobTxn {

    /* Is this txn being cancelled? */
    bool aborting;

    /* List of jobs */
    QLIST_HEAD(, Job) jobs;

    /* Reference count */
    int refcnt;
};

/* Right now, this mutex is only needed to synchronize accesses to job->busy
 * and job->sleep_timer, such as concurrent calls to job_do_yield and
 * job_enter. */
static QemuMutex job_mutex;

static void job_lock(void)
{
    qemu_mutex_lock(&job_mutex);
}

static void job_unlock(void)
{
    qemu_mutex_unlock(&job_mutex);
}

static void __attribute__((__constructor__)) job_init(void)
{
    qemu_mutex_init(&job_mutex);
}

JobTxn *job_txn_new(void)
{
    JobTxn *txn = g_new0(JobTxn, 1);
    QLIST_INIT(&txn->jobs);
    txn->refcnt = 1;
    return txn;
}

static void job_txn_ref(JobTxn *txn)
{
    txn->refcnt++;
}

void job_txn_unref(JobTxn *txn)
{
    if (txn && --txn->refcnt == 0) {
        g_free(txn);
    }
}

void job_txn_add_job(JobTxn *txn, Job *job)
{
    if (!txn) {
        return;
    }

    assert(!job->txn);
    job->txn = txn;

    QLIST_INSERT_HEAD(&txn->jobs, job, txn_list);
    job_txn_ref(txn);
}

static void job_txn_del_job(Job *job)
{
    if (job->txn) {
        QLIST_REMOVE(job, txn_list);
        job_txn_unref(job->txn);
        job->txn = NULL;
    }
}

static int job_txn_apply(Job *job, int fn(Job *))
{
    AioContext *inner_ctx;
    Job *other_job, *next;
    JobTxn *txn = job->txn;
    int rc = 0;

    /*
     * Similar to job_completed_txn_abort, we take each job's lock before
     * applying fn, but since we assume that outer_ctx is held by the caller,
     * we need to release it here to avoid holding the lock twice - which would
     * break AIO_WAIT_WHILE from within fn.
     */
    job_ref(job);
    aio_context_release(job->aio_context);

    QLIST_FOREACH_SAFE(other_job, &txn->jobs, txn_list, next) {
        inner_ctx = other_job->aio_context;
        aio_context_acquire(inner_ctx);
        rc = fn(other_job);
        aio_context_release(inner_ctx);
        if (rc) {
            break;
        }
    }

    /*
     * Note that job->aio_context might have been changed by calling fn, so we
     * can't use a local variable to cache it.
     */
    aio_context_acquire(job->aio_context);
    job_unref(job);
    return rc;
}

bool job_is_internal(Job *job)
{
    return (job->id == NULL);
}

static void job_state_transition(Job *job, JobStatus s1)
{
    JobStatus s0 = job->status;
    assert(s1 >= 0 && s1 < JOB_STATUS__MAX);
    trace_job_state_transition(job, job->ret,
                               JobSTT[s0][s1] ? "allowed" : "disallowed",
                               JobStatus_str(s0), JobStatus_str(s1));
    assert(JobSTT[s0][s1]);
    job->status = s1;

    if (!job_is_internal(job) && s1 != s0) {
        qapi_event_send_job_status_change(job->id, job->status);
    }
}

int job_apply_verb(Job *job, JobVerb verb, Error **errp)
{
    JobStatus s0 = job->status;
    assert(verb >= 0 && verb < JOB_VERB__MAX);
    trace_job_apply_verb(job, JobStatus_str(s0), JobVerb_str(verb),
                         JobVerbTable[verb][s0] ? "allowed" : "prohibited");
    if (JobVerbTable[verb][s0]) {
        return 0;
    }
    error_setg(errp, "Job '%s' in state '%s' cannot accept command verb '%s'",
               job->id, JobStatus_str(s0), JobVerb_str(verb));
    return -EPERM;
}

JobType job_type(const Job *job)
{
    return job->driver->job_type;
}

const char *job_type_str(const Job *job)
{
    return JobType_str(job_type(job));
}

bool job_is_cancelled(Job *job)
{
    return job->cancelled;
}

bool job_is_ready(Job *job)
{
    switch (job->status) {
    case JOB_STATUS_UNDEFINED:
    case JOB_STATUS_CREATED:
    case JOB_STATUS_RUNNING:
    case JOB_STATUS_PAUSED:
    case JOB_STATUS_WAITING:
    case JOB_STATUS_PENDING:
    case JOB_STATUS_ABORTING:
    case JOB_STATUS_CONCLUDED:
    case JOB_STATUS_NULL:
        return false;
    case JOB_STATUS_READY:
    case JOB_STATUS_STANDBY:
        return true;
    default:
        g_assert_not_reached();
    }
    return false;
}

bool job_is_completed(Job *job)
{
    switch (job->status) {
    case JOB_STATUS_UNDEFINED:
    case JOB_STATUS_CREATED:
    case JOB_STATUS_RUNNING:
    case JOB_STATUS_PAUSED:
    case JOB_STATUS_READY:
    case JOB_STATUS_STANDBY:
        return false;
    case JOB_STATUS_WAITING:
    case JOB_STATUS_PENDING:
    case JOB_STATUS_ABORTING:
    case JOB_STATUS_CONCLUDED:
    case JOB_STATUS_NULL:
        return true;
    default:
        g_assert_not_reached();
    }
    return false;
}

static bool job_started(Job *job)
{
    return job->co;
}

static bool job_should_pause(Job *job)
{
    return job->pause_count > 0;
}

Job *job_next(Job *job)
{
    if (!job) {
        return QLIST_FIRST(&jobs);
    }
    return QLIST_NEXT(job, job_list);
}

Job *job_get(const char *id)
{
    Job *job;

    QLIST_FOREACH(job, &jobs, job_list) {
        if (job->id && !strcmp(id, job->id)) {
            return job;
        }
    }

    return NULL;
}

static void job_sleep_timer_cb(void *opaque)
{
    Job *job = opaque;

    job_enter(job);
}

void *job_create(const char *job_id, const JobDriver *driver, JobTxn *txn,
                 AioContext *ctx, int flags, BlockCompletionFunc *cb,
                 void *opaque, Error **errp)
{
    Job *job;

    if (job_id) {
        if (flags & JOB_INTERNAL) {
            error_setg(errp, "Cannot specify job ID for internal job");
            return NULL;
        }
        if (!id_wellformed(job_id)) {
            error_setg(errp, "Invalid job ID '%s'", job_id);
            return NULL;
        }
        if (job_get(job_id)) {
            error_setg(errp, "Job ID '%s' already in use", job_id);
            return NULL;
        }
    } else if (!(flags & JOB_INTERNAL)) {
        error_setg(errp, "An explicit job ID is required");
        return NULL;
    }

    job = g_malloc0(driver->instance_size);
    job->driver        = driver;
    job->id            = g_strdup(job_id);
    job->refcnt        = 1;
    job->aio_context   = ctx;
    job->busy          = false;
    job->paused        = true;
    job->pause_count   = 1;
    job->auto_finalize = !(flags & JOB_MANUAL_FINALIZE);
    job->auto_dismiss  = !(flags & JOB_MANUAL_DISMISS);
    job->cb            = cb;
    job->opaque        = opaque;

    notifier_list_init(&job->on_finalize_cancelled);
    notifier_list_init(&job->on_finalize_completed);
    notifier_list_init(&job->on_pending);
    notifier_list_init(&job->on_ready);

    job_state_transition(job, JOB_STATUS_CREATED);
    aio_timer_init(qemu_get_aio_context(), &job->sleep_timer,
                   QEMU_CLOCK_REALTIME, SCALE_NS,
                   job_sleep_timer_cb, job);

    QLIST_INSERT_HEAD(&jobs, job, job_list);

    /* Single jobs are modeled as single-job transactions for sake of
     * consolidating the job management logic */
    if (!txn) {
        txn = job_txn_new();
        job_txn_add_job(txn, job);
        job_txn_unref(txn);
    } else {
        job_txn_add_job(txn, job);
    }

    return job;
}

void job_ref(Job *job)
{
    ++job->refcnt;
}

void job_unref(Job *job)
{
    if (--job->refcnt == 0) {
        assert(job->status == JOB_STATUS_NULL);
        assert(!timer_pending(&job->sleep_timer));
        assert(!job->txn);

        if (job->driver->free) {
            job->driver->free(job);
        }

        QLIST_REMOVE(job, job_list);

        error_free(job->err);
        g_free(job->id);
        g_free(job);
    }
}

void job_progress_update(Job *job, uint64_t done)
{
    progress_work_done(&job->progress, done);
}

void job_progress_set_remaining(Job *job, uint64_t remaining)
{
    progress_set_remaining(&job->progress, remaining);
}

void job_progress_increase_remaining(Job *job, uint64_t delta)
{
    progress_increase_remaining(&job->progress, delta);
}

void job_event_cancelled(Job *job)
{
    notifier_list_notify(&job->on_finalize_cancelled, job);
}

void job_event_completed(Job *job)
{
    notifier_list_notify(&job->on_finalize_completed, job);
}

static void job_event_pending(Job *job)
{
    notifier_list_notify(&job->on_pending, job);
}

static void job_event_ready(Job *job)
{
    notifier_list_notify(&job->on_ready, job);
}

static void job_event_idle(Job *job)
{
    notifier_list_notify(&job->on_idle, job);
}

void job_enter_cond(Job *job, bool(*fn)(Job *job))
{
    if (!job_started(job)) {
        return;
    }
    if (job->deferred_to_main_loop) {
        return;
    }

    job_lock();
    if (job->busy) {
        job_unlock();
        return;
    }

    if (fn && !fn(job)) {
        job_unlock();
        return;
    }

    assert(!job->deferred_to_main_loop);
    timer_del(&job->sleep_timer);
    job->busy = true;
    job_unlock();
    aio_co_enter(job->aio_context, job->co);
}

void job_enter(Job *job)
{
    job_enter_cond(job, NULL);
}

/* Yield, and schedule a timer to reenter the coroutine after @ns nanoseconds.
 * Reentering the job coroutine with job_enter() before the timer has expired
 * is allowed and cancels the timer.
 *
 * If @ns is (uint64_t) -1, no timer is scheduled and job_enter() must be
 * called explicitly. */
static void coroutine_fn job_do_yield(Job *job, uint64_t ns)
{
    job_lock();
    if (ns != -1) {
        timer_mod(&job->sleep_timer, ns);
    }
    job->busy = false;
    job_event_idle(job);
    job_unlock();
    qemu_coroutine_yield();

    /* Set by job_enter_cond() before re-entering the coroutine.  */
    assert(job->busy);
}

void coroutine_fn job_pause_point(Job *job)
{
    assert(job && job_started(job));

    if (!job_should_pause(job)) {
        return;
    }
    if (job_is_cancelled(job)) {
        return;
    }

    if (job->driver->pause) {
        job->driver->pause(job);
    }

    if (job_should_pause(job) && !job_is_cancelled(job)) {
        JobStatus status = job->status;
        job_state_transition(job, status == JOB_STATUS_READY
                                  ? JOB_STATUS_STANDBY
                                  : JOB_STATUS_PAUSED);
        job->paused = true;
        job_do_yield(job, -1);
        job->paused = false;
        job_state_transition(job, status);
    }

    if (job->driver->resume) {
        job->driver->resume(job);
    }
}

void job_yield(Job *job)
{
    assert(job->busy);

    /* Check cancellation *before* setting busy = false, too!  */
    if (job_is_cancelled(job)) {
        return;
    }

    if (!job_should_pause(job)) {
        job_do_yield(job, -1);
    }

    job_pause_point(job);
}

void coroutine_fn job_sleep_ns(Job *job, int64_t ns)
{
    assert(job->busy);

    /* Check cancellation *before* setting busy = false, too!  */
    if (job_is_cancelled(job)) {
        return;
    }

    if (!job_should_pause(job)) {
        job_do_yield(job, qemu_clock_get_ns(QEMU_CLOCK_REALTIME) + ns);
    }

    job_pause_point(job);
}

/* Assumes the block_job_mutex is held */
static bool job_timer_not_pending(Job *job)
{
    return !timer_pending(&job->sleep_timer);
}

void job_pause(Job *job)
{
    job->pause_count++;
}

void job_resume(Job *job)
{
    assert(job->pause_count > 0);
    job->pause_count--;
    if (job->pause_count) {
        return;
    }

    /* kick only if no timer is pending */
    job_enter_cond(job, job_timer_not_pending);
}

void job_user_pause(Job *job, Error **errp)
{
    if (job_apply_verb(job, JOB_VERB_PAUSE, errp)) {
        return;
    }
    if (job->user_paused) {
        error_setg(errp, "Job is already paused");
        return;
    }
    job->user_paused = true;
    job_pause(job);
}

bool job_user_paused(Job *job)
{
    return job->user_paused;
}

void job_user_resume(Job *job, Error **errp)
{
    assert(job);
    if (!job->user_paused || job->pause_count <= 0) {
        error_setg(errp, "Can't resume a job that was not paused");
        return;
    }
    if (job_apply_verb(job, JOB_VERB_RESUME, errp)) {
        return;
    }
    if (job->driver->user_resume) {
        job->driver->user_resume(job);
    }
    job->user_paused = false;
    job_resume(job);
}

static void job_do_dismiss(Job *job)
{
    assert(job);
    job->busy = false;
    job->paused = false;
    job->deferred_to_main_loop = true;

    job_txn_del_job(job);

    job_state_transition(job, JOB_STATUS_NULL);
    job_unref(job);
}

void job_dismiss(Job **jobptr, Error **errp)
{
    Job *job = *jobptr;
    /* similarly to _complete, this is QMP-interface only. */
    assert(job->id);
    if (job_apply_verb(job, JOB_VERB_DISMISS, errp)) {
        return;
    }

    job_do_dismiss(job);
    *jobptr = NULL;
}

void job_early_fail(Job *job)
{
    assert(job->status == JOB_STATUS_CREATED);
    job_do_dismiss(job);
}

static void job_conclude(Job *job)
{
    job_state_transition(job, JOB_STATUS_CONCLUDED);
    if (job->auto_dismiss || !job_started(job)) {
        job_do_dismiss(job);
    }
}

static void job_update_rc(Job *job)
{
    if (!job->ret && job_is_cancelled(job)) {
        job->ret = -ECANCELED;
    }
    if (job->ret) {
        if (!job->err) {
            error_setg(&job->err, "%s", strerror(-job->ret));
        }
        job_state_transition(job, JOB_STATUS_ABORTING);
    }
}

static void job_commit(Job *job)
{
    assert(!job->ret);
    if (job->driver->commit) {
        job->driver->commit(job);
    }
}

static void job_abort(Job *job)
{
    assert(job->ret);
    if (job->driver->abort) {
        job->driver->abort(job);
    }
}

static void job_clean(Job *job)
{
    if (job->driver->clean) {
        job->driver->clean(job);
    }
}

static int job_finalize_single(Job *job)
{
    assert(job_is_completed(job));

    /* Ensure abort is called for late-transactional failures */
    job_update_rc(job);

    if (!job->ret) {
        job_commit(job);
    } else {
        job_abort(job);
    }
    job_clean(job);

    if (job->cb) {
        job->cb(job->opaque, job->ret);
    }

    /* Emit events only if we actually started */
    if (job_started(job)) {
        if (job_is_cancelled(job)) {
            job_event_cancelled(job);
        } else {
            job_event_completed(job);
        }
    }

    job_txn_del_job(job);
    job_conclude(job);
    return 0;
}

static void job_cancel_async(Job *job, bool force)
{
    if (job->user_paused) {
        /* Do not call job_enter here, the caller will handle it.  */
        if (job->driver->user_resume) {
            job->driver->user_resume(job);
        }
        job->user_paused = false;
        assert(job->pause_count > 0);
        job->pause_count--;
    }
    job->cancelled = true;
    /* To prevent 'force == false' overriding a previous 'force == true' */
    job->force_cancel |= force;
}

static void job_completed_txn_abort(Job *job)
{
    AioContext *outer_ctx = job->aio_context;
    AioContext *ctx;
    JobTxn *txn = job->txn;
    Job *other_job;

    if (txn->aborting) {
        /*
         * We are cancelled by another job, which will handle everything.
         */
        return;
    }
    txn->aborting = true;
    job_txn_ref(txn);

    /* We can only hold the single job's AioContext lock while calling
     * job_finalize_single() because the finalization callbacks can involve
     * calls of AIO_WAIT_WHILE(), which could deadlock otherwise. */
    aio_context_release(outer_ctx);

    /* Other jobs are effectively cancelled by us, set the status for
     * them; this job, however, may or may not be cancelled, depending
     * on the caller, so leave it. */
    QLIST_FOREACH(other_job, &txn->jobs, txn_list) {
        if (other_job != job) {
            ctx = other_job->aio_context;
            aio_context_acquire(ctx);
            job_cancel_async(other_job, false);
            aio_context_release(ctx);
        }
    }
    while (!QLIST_EMPTY(&txn->jobs)) {
        other_job = QLIST_FIRST(&txn->jobs);
        ctx = other_job->aio_context;
        aio_context_acquire(ctx);
        if (!job_is_completed(other_job)) {
            assert(job_is_cancelled(other_job));
            job_finish_sync(other_job, NULL, NULL);
        }
        job_finalize_single(other_job);
        aio_context_release(ctx);
    }

    aio_context_acquire(outer_ctx);

    job_txn_unref(txn);
}

static int job_prepare(Job *job)
{
    if (job->ret == 0 && job->driver->prepare) {
        job->ret = job->driver->prepare(job);
        job_update_rc(job);
    }
    return job->ret;
}

static int job_needs_finalize(Job *job)
{
    return !job->auto_finalize;
}

static void job_do_finalize(Job *job)
{
    int rc;
    assert(job && job->txn);

    /* prepare the transaction to complete */
    rc = job_txn_apply(job, job_prepare);
    if (rc) {
        job_completed_txn_abort(job);
    } else {
        job_txn_apply(job, job_finalize_single);
    }
}

void job_finalize(Job *job, Error **errp)
{
    assert(job && job->id);
    if (job_apply_verb(job, JOB_VERB_FINALIZE, errp)) {
        return;
    }
    job_do_finalize(job);
}

static int job_transition_to_pending(Job *job)
{
    job_state_transition(job, JOB_STATUS_PENDING);
    if (!job->auto_finalize) {
        job_event_pending(job);
    }
    return 0;
}

void job_transition_to_ready(Job *job)
{
    job_state_transition(job, JOB_STATUS_READY);
    job_event_ready(job);
}

static void job_completed_txn_success(Job *job)
{
    JobTxn *txn = job->txn;
    Job *other_job;

    job_state_transition(job, JOB_STATUS_WAITING);

    /*
     * Successful completion, see if there are other running jobs in this
     * txn.
     */
    QLIST_FOREACH(other_job, &txn->jobs, txn_list) {
        if (!job_is_completed(other_job)) {
            return;
        }
        assert(other_job->ret == 0);
    }

    job_txn_apply(job, job_transition_to_pending);

    /* If no jobs need manual finalization, automatically do so */
    if (job_txn_apply(job, job_needs_finalize) == 0) {
        job_do_finalize(job);
    }
}

static void job_completed(Job *job)
{
    assert(job && job->txn && !job_is_completed(job));

    job_update_rc(job);
    trace_job_completed(job, job->ret);
    if (job->ret) {
        job_completed_txn_abort(job);
    } else {
        job_completed_txn_success(job);
    }
}

/** Useful only as a type shim for aio_bh_schedule_oneshot. */
static void job_exit(void *opaque)
{
    Job *job = (Job *)opaque;
    AioContext *ctx;

    job_ref(job);
    aio_context_acquire(job->aio_context);

    /* This is a lie, we're not quiescent, but still doing the completion
     * callbacks. However, completion callbacks tend to involve operations that
     * drain block nodes, and if .drained_poll still returned true, we would
     * deadlock. */
    job->busy = false;
    job_event_idle(job);

    job_completed(job);

    /*
     * Note that calling job_completed can move the job to a different
     * aio_context, so we cannot cache from above. job_txn_apply takes care of
     * acquiring the new lock, and we ref/unref to avoid job_completed freeing
     * the job underneath us.
     */
    ctx = job->aio_context;
    job_unref(job);
    aio_context_release(ctx);
}

/**
 * All jobs must allow a pause point before entering their job proper. This
 * ensures that jobs can be paused prior to being started, then resumed later.
 */
static void coroutine_fn job_co_entry(void *opaque)
{
    Job *job = opaque;

    assert(job && job->driver && job->driver->run);
    job_pause_point(job);
    job->ret = job->driver->run(job, &job->err);
    job->deferred_to_main_loop = true;
    job->busy = true;
    aio_bh_schedule_oneshot(qemu_get_aio_context(), job_exit, job);
}

void job_start(Job *job)
{
    assert(job && !job_started(job) && job->paused &&
           job->driver && job->driver->run);
    job->co = qemu_coroutine_create(job_co_entry, job);
    job->pause_count--;
    job->busy = true;
    job->paused = false;
    job_state_transition(job, JOB_STATUS_RUNNING);
    aio_co_enter(job->aio_context, job->co);
}

void job_cancel(Job *job, bool force)
{
    if (job->status == JOB_STATUS_CONCLUDED) {
        job_do_dismiss(job);
        return;
    }
    job_cancel_async(job, force);
    if (!job_started(job)) {
        job_completed(job);
    } else if (job->deferred_to_main_loop) {
        job_completed_txn_abort(job);
    } else {
        job_enter(job);
    }
}

void job_user_cancel(Job *job, bool force, Error **errp)
{
    if (job_apply_verb(job, JOB_VERB_CANCEL, errp)) {
        return;
    }
    job_cancel(job, force);
}

/* A wrapper around job_cancel() taking an Error ** parameter so it may be
 * used with job_finish_sync() without the need for (rather nasty) function
 * pointer casts there. */
static void job_cancel_err(Job *job, Error **errp)
{
    job_cancel(job, false);
}

int job_cancel_sync(Job *job)
{
    return job_finish_sync(job, &job_cancel_err, NULL);
}

void job_cancel_sync_all(void)
{
    Job *job;
    AioContext *aio_context;

    while ((job = job_next(NULL))) {
        aio_context = job->aio_context;
        aio_context_acquire(aio_context);
        job_cancel_sync(job);
        aio_context_release(aio_context);
    }
}

int job_complete_sync(Job *job, Error **errp)
{
    return job_finish_sync(job, job_complete, errp);
}

void job_complete(Job *job, Error **errp)
{
    /* Should not be reachable via external interface for internal jobs */
    assert(job->id);
    if (job_apply_verb(job, JOB_VERB_COMPLETE, errp)) {
        return;
    }
    if (job->pause_count || job_is_cancelled(job) || !job->driver->complete) {
        error_setg(errp, "The active block job '%s' cannot be completed",
                   job->id);
        return;
    }

    job->driver->complete(job, errp);
}

int job_finish_sync(Job *job, void (*finish)(Job *, Error **errp), Error **errp)
{
    Error *local_err = NULL;
    int ret;

    job_ref(job);

    if (finish) {
        finish(job, &local_err);
    }
    if (local_err) {
        error_propagate(errp, local_err);
        job_unref(job);
        return -EBUSY;
    }

    AIO_WAIT_WHILE(job->aio_context,
                   (job_enter(job), !job_is_completed(job)));

    ret = (job_is_cancelled(job) && job->ret == 0) ? -ECANCELED : job->ret;
    job_unref(job);
    return ret;
}
