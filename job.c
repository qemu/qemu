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
#include "trace/trace-root.h"
#include "qapi/qapi-events-job.h"

/*
 * The job API is composed of two categories of functions.
 *
 * The first includes functions used by the monitor.  The monitor is
 * peculiar in that it accesses the job list with job_get, and
 * therefore needs consistency across job_get and the actual operation
 * (e.g. job_user_cancel). To achieve this consistency, the caller
 * calls job_lock/job_unlock itself around the whole operation.
 *
 *
 * The second includes functions used by the job drivers and sometimes
 * by the core block layer. These delegate the locking to the callee instead.
 */

/*
 * job_mutex protects the jobs list, but also makes the
 * struct job fields thread-safe.
 */
QemuMutex job_mutex;

/* Protected by job_mutex */
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
    [JOB_VERB_COMPLETE]             = {0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0},
    [JOB_VERB_FINALIZE]             = {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0},
    [JOB_VERB_DISMISS]              = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [JOB_VERB_CHANGE]               = {0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0},
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

void job_lock(void)
{
    qemu_mutex_lock(&job_mutex);
}

void job_unlock(void)
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

/* Called with job_mutex held. */
static void job_txn_ref_locked(JobTxn *txn)
{
    txn->refcnt++;
}

void job_txn_unref_locked(JobTxn *txn)
{
    if (txn && --txn->refcnt == 0) {
        g_free(txn);
    }
}

void job_txn_unref(JobTxn *txn)
{
    JOB_LOCK_GUARD();
    job_txn_unref_locked(txn);
}

/**
 * @txn: The transaction (may be NULL)
 * @job: Job to add to the transaction
 *
 * Add @job to the transaction.  The @job must not already be in a transaction.
 * The caller must call either job_txn_unref() or job_completed() to release
 * the reference that is automatically grabbed here.
 *
 * If @txn is NULL, the function does nothing.
 *
 * Called with job_mutex held.
 */
static void job_txn_add_job_locked(JobTxn *txn, Job *job)
{
    if (!txn) {
        return;
    }

    assert(!job->txn);
    job->txn = txn;

    QLIST_INSERT_HEAD(&txn->jobs, job, txn_list);
    job_txn_ref_locked(txn);
}

/* Called with job_mutex held. */
static void job_txn_del_job_locked(Job *job)
{
    if (job->txn) {
        QLIST_REMOVE(job, txn_list);
        job_txn_unref_locked(job->txn);
        job->txn = NULL;
    }
}

/* Called with job_mutex held, but releases it temporarily. */
static int job_txn_apply_locked(Job *job, int fn(Job *))
{
    Job *other_job, *next;
    JobTxn *txn = job->txn;
    int rc = 0;

    /*
     * Similar to job_completed_txn_abort, we take each job's lock before
     * applying fn, but since we assume that outer_ctx is held by the caller,
     * we need to release it here to avoid holding the lock twice - which would
     * break AIO_WAIT_WHILE from within fn.
     */
    job_ref_locked(job);

    QLIST_FOREACH_SAFE(other_job, &txn->jobs, txn_list, next) {
        rc = fn(other_job);
        if (rc) {
            break;
        }
    }

    job_unref_locked(job);
    return rc;
}

bool job_is_internal(Job *job)
{
    return (job->id == NULL);
}

/* Called with job_mutex held. */
static void job_state_transition_locked(Job *job, JobStatus s1)
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

int job_apply_verb_locked(Job *job, JobVerb verb, Error **errp)
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

bool job_is_cancelled_locked(Job *job)
{
    /* force_cancel may be true only if cancelled is true, too */
    assert(job->cancelled || !job->force_cancel);
    return job->force_cancel;
}

bool job_is_paused(Job *job)
{
    JOB_LOCK_GUARD();
    return job->paused;
}

bool job_is_cancelled(Job *job)
{
    JOB_LOCK_GUARD();
    return job_is_cancelled_locked(job);
}

/* Called with job_mutex held. */
static bool job_cancel_requested_locked(Job *job)
{
    return job->cancelled;
}

bool job_cancel_requested(Job *job)
{
    JOB_LOCK_GUARD();
    return job_cancel_requested_locked(job);
}

bool job_is_ready_locked(Job *job)
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

bool job_is_ready(Job *job)
{
    JOB_LOCK_GUARD();
    return job_is_ready_locked(job);
}

bool job_is_completed_locked(Job *job)
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

static bool job_is_completed(Job *job)
{
    JOB_LOCK_GUARD();
    return job_is_completed_locked(job);
}

static bool job_started_locked(Job *job)
{
    return job->co;
}

/* Called with job_mutex held. */
static bool job_should_pause_locked(Job *job)
{
    return job->pause_count > 0;
}

Job *job_next_locked(Job *job)
{
    if (!job) {
        return QLIST_FIRST(&jobs);
    }
    return QLIST_NEXT(job, job_list);
}

Job *job_next(Job *job)
{
    JOB_LOCK_GUARD();
    return job_next_locked(job);
}

Job *job_get_locked(const char *id)
{
    Job *job;

    QLIST_FOREACH(job, &jobs, job_list) {
        if (job->id && !strcmp(id, job->id)) {
            return job;
        }
    }

    return NULL;
}

void job_set_aio_context(Job *job, AioContext *ctx)
{
    /* protect against read in job_finish_sync_locked and job_start */
    GLOBAL_STATE_CODE();
    /* protect against read in job_do_yield_locked */
    JOB_LOCK_GUARD();
    /* ensure the job is quiescent while the AioContext is changed */
    assert(job->paused || job_is_completed_locked(job));
    job->aio_context = ctx;
}

/* Called with job_mutex *not* held. */
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

    JOB_LOCK_GUARD();

    if (job_id) {
        if (flags & JOB_INTERNAL) {
            error_setg(errp, "Cannot specify job ID for internal job");
            return NULL;
        }
        if (!id_wellformed(job_id)) {
            error_setg(errp, "Invalid job ID '%s'", job_id);
            return NULL;
        }
        if (job_get_locked(job_id)) {
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

    progress_init(&job->progress);

    notifier_list_init(&job->on_finalize_cancelled);
    notifier_list_init(&job->on_finalize_completed);
    notifier_list_init(&job->on_pending);
    notifier_list_init(&job->on_ready);
    notifier_list_init(&job->on_idle);

    job_state_transition_locked(job, JOB_STATUS_CREATED);
    aio_timer_init(qemu_get_aio_context(), &job->sleep_timer,
                   QEMU_CLOCK_REALTIME, SCALE_NS,
                   job_sleep_timer_cb, job);

    QLIST_INSERT_HEAD(&jobs, job, job_list);

    /* Single jobs are modeled as single-job transactions for sake of
     * consolidating the job management logic */
    if (!txn) {
        txn = job_txn_new();
        job_txn_add_job_locked(txn, job);
        job_txn_unref_locked(txn);
    } else {
        job_txn_add_job_locked(txn, job);
    }

    return job;
}

void job_ref_locked(Job *job)
{
    ++job->refcnt;
}

void job_unref_locked(Job *job)
{
    GLOBAL_STATE_CODE();

    if (--job->refcnt == 0) {
        assert(job->status == JOB_STATUS_NULL);
        assert(!timer_pending(&job->sleep_timer));
        assert(!job->txn);

        if (job->driver->free) {
            job_unlock();
            job->driver->free(job);
            job_lock();
        }

        QLIST_REMOVE(job, job_list);

        progress_destroy(&job->progress);
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

/**
 * To be called when a cancelled job is finalised.
 * Called with job_mutex held.
 */
static void job_event_cancelled_locked(Job *job)
{
    notifier_list_notify(&job->on_finalize_cancelled, job);
}

/**
 * To be called when a successfully completed job is finalised.
 * Called with job_mutex held.
 */
static void job_event_completed_locked(Job *job)
{
    notifier_list_notify(&job->on_finalize_completed, job);
}

/* Called with job_mutex held. */
static void job_event_pending_locked(Job *job)
{
    notifier_list_notify(&job->on_pending, job);
}

/* Called with job_mutex held. */
static void job_event_ready_locked(Job *job)
{
    notifier_list_notify(&job->on_ready, job);
}

/* Called with job_mutex held. */
static void job_event_idle_locked(Job *job)
{
    notifier_list_notify(&job->on_idle, job);
}

void job_enter_cond_locked(Job *job, bool(*fn)(Job *job))
{
    if (!job_started_locked(job)) {
        return;
    }
    if (job->deferred_to_main_loop) {
        return;
    }

    if (job->busy) {
        return;
    }

    if (fn && !fn(job)) {
        return;
    }

    assert(!job->deferred_to_main_loop);
    timer_del(&job->sleep_timer);
    job->busy = true;
    job_unlock();
    aio_co_wake(job->co);
    job_lock();
}

void job_enter(Job *job)
{
    JOB_LOCK_GUARD();
    job_enter_cond_locked(job, NULL);
}

/* Yield, and schedule a timer to reenter the coroutine after @ns nanoseconds.
 * Reentering the job coroutine with job_enter() before the timer has expired
 * is allowed and cancels the timer.
 *
 * If @ns is (uint64_t) -1, no timer is scheduled and job_enter() must be
 * called explicitly.
 *
 * Called with job_mutex held, but releases it temporarily.
 */
static void coroutine_fn job_do_yield_locked(Job *job, uint64_t ns)
{
    AioContext *next_aio_context;

    if (ns != -1) {
        timer_mod(&job->sleep_timer, ns);
    }
    job->busy = false;
    job_event_idle_locked(job);
    job_unlock();
    qemu_coroutine_yield();
    job_lock();

    next_aio_context = job->aio_context;
    /*
     * Coroutine has resumed, but in the meanwhile the job AioContext
     * might have changed via bdrv_try_change_aio_context(), so we need to move
     * the coroutine too in the new aiocontext.
     */
    while (qemu_get_current_aio_context() != next_aio_context) {
        job_unlock();
        aio_co_reschedule_self(next_aio_context);
        job_lock();
        next_aio_context = job->aio_context;
    }

    /* Set by job_enter_cond_locked() before re-entering the coroutine.  */
    assert(job->busy);
}

/* Called with job_mutex held, but releases it temporarily. */
static void coroutine_fn job_pause_point_locked(Job *job)
{
    assert(job && job_started_locked(job));

    if (!job_should_pause_locked(job)) {
        return;
    }
    if (job_is_cancelled_locked(job)) {
        return;
    }

    if (job->driver->pause) {
        job_unlock();
        job->driver->pause(job);
        job_lock();
    }

    if (job_should_pause_locked(job) && !job_is_cancelled_locked(job)) {
        JobStatus status = job->status;
        job_state_transition_locked(job, status == JOB_STATUS_READY
                                    ? JOB_STATUS_STANDBY
                                    : JOB_STATUS_PAUSED);
        job->paused = true;
        job_do_yield_locked(job, -1);
        job->paused = false;
        job_state_transition_locked(job, status);
    }

    if (job->driver->resume) {
        job_unlock();
        job->driver->resume(job);
        job_lock();
    }
}

void coroutine_fn job_pause_point(Job *job)
{
    JOB_LOCK_GUARD();
    job_pause_point_locked(job);
}

void coroutine_fn job_yield(Job *job)
{
    JOB_LOCK_GUARD();
    assert(job->busy);

    /* Check cancellation *before* setting busy = false, too!  */
    if (job_is_cancelled_locked(job)) {
        return;
    }

    if (!job_should_pause_locked(job)) {
        job_do_yield_locked(job, -1);
    }

    job_pause_point_locked(job);
}

void coroutine_fn job_sleep_ns(Job *job, int64_t ns)
{
    JOB_LOCK_GUARD();
    assert(job->busy);

    /* Check cancellation *before* setting busy = false, too!  */
    if (job_is_cancelled_locked(job)) {
        return;
    }

    if (!job_should_pause_locked(job)) {
        job_do_yield_locked(job, qemu_clock_get_ns(QEMU_CLOCK_REALTIME) + ns);
    }

    job_pause_point_locked(job);
}

/* Assumes the job_mutex is held */
static bool job_timer_not_pending_locked(Job *job)
{
    return !timer_pending(&job->sleep_timer);
}

void job_pause_locked(Job *job)
{
    job->pause_count++;
    if (!job->paused) {
        job_enter_cond_locked(job, NULL);
    }
}

void job_pause(Job *job)
{
    JOB_LOCK_GUARD();
    job_pause_locked(job);
}

void job_resume_locked(Job *job)
{
    assert(job->pause_count > 0);
    job->pause_count--;
    if (job->pause_count) {
        return;
    }

    /* kick only if no timer is pending */
    job_enter_cond_locked(job, job_timer_not_pending_locked);
}

void job_resume(Job *job)
{
    JOB_LOCK_GUARD();
    job_resume_locked(job);
}

void job_user_pause_locked(Job *job, Error **errp)
{
    if (job_apply_verb_locked(job, JOB_VERB_PAUSE, errp)) {
        return;
    }
    if (job->user_paused) {
        error_setg(errp, "Job is already paused");
        return;
    }
    job->user_paused = true;
    job_pause_locked(job);
}

bool job_user_paused_locked(Job *job)
{
    return job->user_paused;
}

void job_user_resume_locked(Job *job, Error **errp)
{
    assert(job);
    GLOBAL_STATE_CODE();
    if (!job->user_paused || job->pause_count <= 0) {
        error_setg(errp, "Can't resume a job that was not paused");
        return;
    }
    if (job_apply_verb_locked(job, JOB_VERB_RESUME, errp)) {
        return;
    }
    if (job->driver->user_resume) {
        job_unlock();
        job->driver->user_resume(job);
        job_lock();
    }
    job->user_paused = false;
    job_resume_locked(job);
}

/* Called with job_mutex held, but releases it temporarily. */
static void job_do_dismiss_locked(Job *job)
{
    assert(job);
    job->busy = false;
    job->paused = false;
    job->deferred_to_main_loop = true;

    job_txn_del_job_locked(job);

    job_state_transition_locked(job, JOB_STATUS_NULL);
    job_unref_locked(job);
}

void job_dismiss_locked(Job **jobptr, Error **errp)
{
    Job *job = *jobptr;
    /* similarly to _complete, this is QMP-interface only. */
    assert(job->id);
    if (job_apply_verb_locked(job, JOB_VERB_DISMISS, errp)) {
        return;
    }

    job_do_dismiss_locked(job);
    *jobptr = NULL;
}

void job_early_fail(Job *job)
{
    JOB_LOCK_GUARD();
    assert(job->status == JOB_STATUS_CREATED);
    job_do_dismiss_locked(job);
}

/* Called with job_mutex held. */
static void job_conclude_locked(Job *job)
{
    job_state_transition_locked(job, JOB_STATUS_CONCLUDED);
    if (job->auto_dismiss || !job_started_locked(job)) {
        job_do_dismiss_locked(job);
    }
}

/* Called with job_mutex held. */
static void job_update_rc_locked(Job *job)
{
    if (!job->ret && job_is_cancelled_locked(job)) {
        job->ret = -ECANCELED;
    }
    if (job->ret) {
        if (!job->err) {
            error_setg(&job->err, "%s", strerror(-job->ret));
        }
        job_state_transition_locked(job, JOB_STATUS_ABORTING);
    }
}

static void job_commit(Job *job)
{
    assert(!job->ret);
    GLOBAL_STATE_CODE();
    if (job->driver->commit) {
        job->driver->commit(job);
    }
}

static void job_abort(Job *job)
{
    assert(job->ret);
    GLOBAL_STATE_CODE();
    if (job->driver->abort) {
        job->driver->abort(job);
    }
}

static void job_clean(Job *job)
{
    GLOBAL_STATE_CODE();
    if (job->driver->clean) {
        job->driver->clean(job);
    }
}

/*
 * Called with job_mutex held, but releases it temporarily.
 */
static int job_finalize_single_locked(Job *job)
{
    int job_ret;

    assert(job_is_completed_locked(job));

    /* Ensure abort is called for late-transactional failures */
    job_update_rc_locked(job);

    job_ret = job->ret;
    job_unlock();

    if (!job_ret) {
        job_commit(job);
    } else {
        job_abort(job);
    }
    job_clean(job);

    if (job->cb) {
        job->cb(job->opaque, job_ret);
    }

    job_lock();

    /* Emit events only if we actually started */
    if (job_started_locked(job)) {
        if (job_is_cancelled_locked(job)) {
            job_event_cancelled_locked(job);
        } else {
            job_event_completed_locked(job);
        }
    }

    job_txn_del_job_locked(job);
    job_conclude_locked(job);
    return 0;
}

/*
 * Called with job_mutex held, but releases it temporarily.
 */
static void job_cancel_async_locked(Job *job, bool force)
{
    GLOBAL_STATE_CODE();
    if (job->driver->cancel) {
        job_unlock();
        force = job->driver->cancel(job, force);
        job_lock();
    } else {
        /* No .cancel() means the job will behave as if force-cancelled */
        force = true;
    }

    if (job->user_paused) {
        /* Do not call job_enter here, the caller will handle it.  */
        if (job->driver->user_resume) {
            job_unlock();
            job->driver->user_resume(job);
            job_lock();
        }
        job->user_paused = false;
        assert(job->pause_count > 0);
        job->pause_count--;
    }

    /*
     * Ignore soft cancel requests after the job is already done
     * (We will still invoke job->driver->cancel() above, but if the
     * job driver supports soft cancelling and the job is done, that
     * should be a no-op, too.  We still call it so it can override
     * @force.)
     */
    if (force || !job->deferred_to_main_loop) {
        job->cancelled = true;
        /* To prevent 'force == false' overriding a previous 'force == true' */
        job->force_cancel |= force;
    }
}

/*
 * Called with job_mutex held, but releases it temporarily.
 */
static void job_completed_txn_abort_locked(Job *job)
{
    JobTxn *txn = job->txn;
    Job *other_job;

    if (txn->aborting) {
        /*
         * We are cancelled by another job, which will handle everything.
         */
        return;
    }
    txn->aborting = true;
    job_txn_ref_locked(txn);

    job_ref_locked(job);

    /* Other jobs are effectively cancelled by us, set the status for
     * them; this job, however, may or may not be cancelled, depending
     * on the caller, so leave it. */
    QLIST_FOREACH(other_job, &txn->jobs, txn_list) {
        if (other_job != job) {
            /*
             * This is a transaction: If one job failed, no result will matter.
             * Therefore, pass force=true to terminate all other jobs as quickly
             * as possible.
             */
            job_cancel_async_locked(other_job, true);
        }
    }
    while (!QLIST_EMPTY(&txn->jobs)) {
        other_job = QLIST_FIRST(&txn->jobs);
        if (!job_is_completed_locked(other_job)) {
            assert(job_cancel_requested_locked(other_job));
            job_finish_sync_locked(other_job, NULL, NULL);
        }
        job_finalize_single_locked(other_job);
    }

    job_unref_locked(job);
    job_txn_unref_locked(txn);
}

/* Called with job_mutex held, but releases it temporarily */
static int job_prepare_locked(Job *job)
{
    int ret;

    GLOBAL_STATE_CODE();

    if (job->ret == 0 && job->driver->prepare) {
        job_unlock();
        ret = job->driver->prepare(job);
        job_lock();
        job->ret = ret;
        job_update_rc_locked(job);
    }

    return job->ret;
}

/* Called with job_mutex held */
static int job_needs_finalize_locked(Job *job)
{
    return !job->auto_finalize;
}

/* Called with job_mutex held */
static void job_do_finalize_locked(Job *job)
{
    int rc;
    assert(job && job->txn);

    /* prepare the transaction to complete */
    rc = job_txn_apply_locked(job, job_prepare_locked);
    if (rc) {
        job_completed_txn_abort_locked(job);
    } else {
        job_txn_apply_locked(job, job_finalize_single_locked);
    }
}

void job_finalize_locked(Job *job, Error **errp)
{
    assert(job && job->id);
    if (job_apply_verb_locked(job, JOB_VERB_FINALIZE, errp)) {
        return;
    }
    job_do_finalize_locked(job);
}

/* Called with job_mutex held. */
static int job_transition_to_pending_locked(Job *job)
{
    job_state_transition_locked(job, JOB_STATUS_PENDING);
    if (!job->auto_finalize) {
        job_event_pending_locked(job);
    }
    return 0;
}

void job_transition_to_ready(Job *job)
{
    JOB_LOCK_GUARD();
    job_state_transition_locked(job, JOB_STATUS_READY);
    job_event_ready_locked(job);
}

/* Called with job_mutex held. */
static void job_completed_txn_success_locked(Job *job)
{
    JobTxn *txn = job->txn;
    Job *other_job;

    job_state_transition_locked(job, JOB_STATUS_WAITING);

    /*
     * Successful completion, see if there are other running jobs in this
     * txn.
     */
    QLIST_FOREACH(other_job, &txn->jobs, txn_list) {
        if (!job_is_completed_locked(other_job)) {
            return;
        }
        assert(other_job->ret == 0);
    }

    job_txn_apply_locked(job, job_transition_to_pending_locked);

    /* If no jobs need manual finalization, automatically do so */
    if (job_txn_apply_locked(job, job_needs_finalize_locked) == 0) {
        job_do_finalize_locked(job);
    }
}

/* Called with job_mutex held. */
static void job_completed_locked(Job *job)
{
    assert(job && job->txn && !job_is_completed_locked(job));

    job_update_rc_locked(job);
    trace_job_completed(job, job->ret);
    if (job->ret) {
        job_completed_txn_abort_locked(job);
    } else {
        job_completed_txn_success_locked(job);
    }
}

/**
 * Useful only as a type shim for aio_bh_schedule_oneshot.
 * Called with job_mutex *not* held.
 */
static void job_exit(void *opaque)
{
    Job *job = (Job *)opaque;
    JOB_LOCK_GUARD();
    job_ref_locked(job);

    /* This is a lie, we're not quiescent, but still doing the completion
     * callbacks. However, completion callbacks tend to involve operations that
     * drain block nodes, and if .drained_poll still returned true, we would
     * deadlock. */
    job->busy = false;
    job_event_idle_locked(job);

    job_completed_locked(job);
    job_unref_locked(job);
}

/**
 * All jobs must allow a pause point before entering their job proper. This
 * ensures that jobs can be paused prior to being started, then resumed later.
 */
static void coroutine_fn job_co_entry(void *opaque)
{
    Job *job = opaque;
    int ret;

    assert(job && job->driver && job->driver->run);
    WITH_JOB_LOCK_GUARD() {
        assert(job->aio_context == qemu_get_current_aio_context());
        job_pause_point_locked(job);
    }
    ret = job->driver->run(job, &job->err);
    WITH_JOB_LOCK_GUARD() {
        job->ret = ret;
        job->deferred_to_main_loop = true;
        job->busy = true;
    }
    aio_bh_schedule_oneshot(qemu_get_aio_context(), job_exit, job);
}

void job_start(Job *job)
{
    assert(qemu_in_main_thread());

    WITH_JOB_LOCK_GUARD() {
        assert(job && !job_started_locked(job) && job->paused &&
            job->driver && job->driver->run);
        job->co = qemu_coroutine_create(job_co_entry, job);
        job->pause_count--;
        job->busy = true;
        job->paused = false;
        job_state_transition_locked(job, JOB_STATUS_RUNNING);
    }
    aio_co_enter(job->aio_context, job->co);
}

void job_cancel_locked(Job *job, bool force)
{
    if (job->status == JOB_STATUS_CONCLUDED) {
        job_do_dismiss_locked(job);
        return;
    }
    job_cancel_async_locked(job, force);
    if (!job_started_locked(job)) {
        job_completed_locked(job);
    } else if (job->deferred_to_main_loop) {
        /*
         * job_cancel_async() ignores soft-cancel requests for jobs
         * that are already done (i.e. deferred to the main loop).  We
         * have to check again whether the job is really cancelled.
         * (job_cancel_requested() and job_is_cancelled() are equivalent
         * here, because job_cancel_async() will make soft-cancel
         * requests no-ops when deferred_to_main_loop is true.  We
         * choose to call job_is_cancelled() to show that we invoke
         * job_completed_txn_abort() only for force-cancelled jobs.)
         */
        if (job_is_cancelled_locked(job)) {
            job_completed_txn_abort_locked(job);
        }
    } else {
        job_enter_cond_locked(job, NULL);
    }
}

void job_user_cancel_locked(Job *job, bool force, Error **errp)
{
    if (job_apply_verb_locked(job, JOB_VERB_CANCEL, errp)) {
        return;
    }
    job_cancel_locked(job, force);
}

/* A wrapper around job_cancel_locked() taking an Error ** parameter so it may
 * be used with job_finish_sync_locked() without the need for (rather nasty)
 * function pointer casts there.
 *
 * Called with job_mutex held.
 */
static void job_cancel_err_locked(Job *job, Error **errp)
{
    job_cancel_locked(job, false);
}

/**
 * Same as job_cancel_err(), but force-cancel.
 * Called with job_mutex held.
 */
static void job_force_cancel_err_locked(Job *job, Error **errp)
{
    job_cancel_locked(job, true);
}

int job_cancel_sync_locked(Job *job, bool force)
{
    if (force) {
        return job_finish_sync_locked(job, &job_force_cancel_err_locked, NULL);
    } else {
        return job_finish_sync_locked(job, &job_cancel_err_locked, NULL);
    }
}

int job_cancel_sync(Job *job, bool force)
{
    JOB_LOCK_GUARD();
    return job_cancel_sync_locked(job, force);
}

void job_cancel_sync_all(void)
{
    Job *job;
    JOB_LOCK_GUARD();

    while ((job = job_next_locked(NULL))) {
        job_cancel_sync_locked(job, true);
    }
}

int job_complete_sync_locked(Job *job, Error **errp)
{
    return job_finish_sync_locked(job, job_complete_locked, errp);
}

void job_complete_locked(Job *job, Error **errp)
{
    /* Should not be reachable via external interface for internal jobs */
    assert(job->id);
    GLOBAL_STATE_CODE();
    if (job_apply_verb_locked(job, JOB_VERB_COMPLETE, errp)) {
        return;
    }
    if (job_cancel_requested_locked(job) || !job->driver->complete) {
        error_setg(errp, "The active block job '%s' cannot be completed",
                   job->id);
        return;
    }

    job_unlock();
    job->driver->complete(job, errp);
    job_lock();
}

int job_finish_sync_locked(Job *job,
                           void (*finish)(Job *, Error **errp),
                           Error **errp)
{
    Error *local_err = NULL;
    int ret;
    GLOBAL_STATE_CODE();

    job_ref_locked(job);

    if (finish) {
        finish(job, &local_err);
    }
    if (local_err) {
        error_propagate(errp, local_err);
        job_unref_locked(job);
        return -EBUSY;
    }

    job_unlock();
    AIO_WAIT_WHILE_UNLOCKED(job->aio_context,
                            (job_enter(job), !job_is_completed(job)));
    job_lock();

    ret = (job_is_cancelled_locked(job) && job->ret == 0)
          ? -ECANCELED : job->ret;
    job_unref_locked(job);
    return ret;
}
