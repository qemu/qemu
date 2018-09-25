/*
 * Data plane event loop
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2009-2017 QEMU contributors
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
#include "qemu-common.h"
#include "block/aio.h"
#include "block/thread-pool.h"
#include "qemu/main-loop.h"
#include "qemu/atomic.h"
#include "block/raw-aio.h"
#include "qemu/coroutine_int.h"
#include "trace.h"

/***********************************************************/
/* bottom halves (can be seen as timers which expire ASAP) */

struct QEMUBH {
    AioContext *ctx;
    QEMUBHFunc *cb;
    void *opaque;
    QEMUBH *next;
    bool scheduled;
    bool idle;
    bool deleted;
};

void aio_bh_schedule_oneshot(AioContext *ctx, QEMUBHFunc *cb, void *opaque)
{
    QEMUBH *bh;
    bh = g_new(QEMUBH, 1);
    *bh = (QEMUBH){
        .ctx = ctx,
        .cb = cb,
        .opaque = opaque,
    };
    qemu_lockcnt_lock(&ctx->list_lock);
    bh->next = ctx->first_bh;
    bh->scheduled = 1;
    bh->deleted = 1;
    /* Make sure that the members are ready before putting bh into list */
    smp_wmb();
    ctx->first_bh = bh;
    qemu_lockcnt_unlock(&ctx->list_lock);
    aio_notify(ctx);
}

QEMUBH *aio_bh_new(AioContext *ctx, QEMUBHFunc *cb, void *opaque)
{
    QEMUBH *bh;
    bh = g_new(QEMUBH, 1);
    *bh = (QEMUBH){
        .ctx = ctx,
        .cb = cb,
        .opaque = opaque,
    };
    qemu_lockcnt_lock(&ctx->list_lock);
    bh->next = ctx->first_bh;
    /* Make sure that the members are ready before putting bh into list */
    smp_wmb();
    ctx->first_bh = bh;
    qemu_lockcnt_unlock(&ctx->list_lock);
    return bh;
}

void aio_bh_call(QEMUBH *bh)
{
    bh->cb(bh->opaque);
}

/* Multiple occurrences of aio_bh_poll cannot be called concurrently.
 * The count in ctx->list_lock is incremented before the call, and is
 * not affected by the call.
 */
int aio_bh_poll(AioContext *ctx)
{
    QEMUBH *bh, **bhp, *next;
    int ret;
    bool deleted = false;

    ret = 0;
    for (bh = atomic_rcu_read(&ctx->first_bh); bh; bh = next) {
        next = atomic_rcu_read(&bh->next);
        /* The atomic_xchg is paired with the one in qemu_bh_schedule.  The
         * implicit memory barrier ensures that the callback sees all writes
         * done by the scheduling thread.  It also ensures that the scheduling
         * thread sees the zero before bh->cb has run, and thus will call
         * aio_notify again if necessary.
         */
        if (atomic_xchg(&bh->scheduled, 0)) {
            /* Idle BHs don't count as progress */
            if (!bh->idle) {
                ret = 1;
            }
            bh->idle = 0;
            aio_bh_call(bh);
        }
        if (bh->deleted) {
            deleted = true;
        }
    }

    /* remove deleted bhs */
    if (!deleted) {
        return ret;
    }

    if (qemu_lockcnt_dec_if_lock(&ctx->list_lock)) {
        bhp = &ctx->first_bh;
        while (*bhp) {
            bh = *bhp;
            if (bh->deleted && !bh->scheduled) {
                *bhp = bh->next;
                g_free(bh);
            } else {
                bhp = &bh->next;
            }
        }
        qemu_lockcnt_inc_and_unlock(&ctx->list_lock);
    }
    return ret;
}

void qemu_bh_schedule_idle(QEMUBH *bh)
{
    bh->idle = 1;
    /* Make sure that idle & any writes needed by the callback are done
     * before the locations are read in the aio_bh_poll.
     */
    atomic_mb_set(&bh->scheduled, 1);
}

void qemu_bh_schedule(QEMUBH *bh)
{
    AioContext *ctx;

    ctx = bh->ctx;
    bh->idle = 0;
    /* The memory barrier implicit in atomic_xchg makes sure that:
     * 1. idle & any writes needed by the callback are done before the
     *    locations are read in the aio_bh_poll.
     * 2. ctx is loaded before scheduled is set and the callback has a chance
     *    to execute.
     */
    if (atomic_xchg(&bh->scheduled, 1) == 0) {
        aio_notify(ctx);
    }
}


/* This func is async.
 */
void qemu_bh_cancel(QEMUBH *bh)
{
    atomic_mb_set(&bh->scheduled, 0);
}

/* This func is async.The bottom half will do the delete action at the finial
 * end.
 */
void qemu_bh_delete(QEMUBH *bh)
{
    bh->scheduled = 0;
    bh->deleted = 1;
}

int64_t
aio_compute_timeout(AioContext *ctx)
{
    int64_t deadline;
    int timeout = -1;
    QEMUBH *bh;

    for (bh = atomic_rcu_read(&ctx->first_bh); bh;
         bh = atomic_rcu_read(&bh->next)) {
        if (bh->scheduled) {
            if (bh->idle) {
                /* idle bottom halves will be polled at least
                 * every 10ms */
                timeout = 10000000;
            } else {
                /* non-idle bottom halves will be executed
                 * immediately */
                return 0;
            }
        }
    }

    deadline = timerlistgroup_deadline_ns(&ctx->tlg);
    if (deadline == 0) {
        return 0;
    } else {
        return qemu_soonest_timeout(timeout, deadline);
    }
}

static gboolean
aio_ctx_prepare(GSource *source, gint    *timeout)
{
    AioContext *ctx = (AioContext *) source;

    atomic_or(&ctx->notify_me, 1);

    /* We assume there is no timeout already supplied */
    *timeout = qemu_timeout_ns_to_ms(aio_compute_timeout(ctx));

    if (aio_prepare(ctx)) {
        *timeout = 0;
    }

    return *timeout == 0;
}

static gboolean
aio_ctx_check(GSource *source)
{
    AioContext *ctx = (AioContext *) source;
    QEMUBH *bh;

    atomic_and(&ctx->notify_me, ~1);
    aio_notify_accept(ctx);

    for (bh = ctx->first_bh; bh; bh = bh->next) {
        if (bh->scheduled) {
            return true;
        }
    }
    return aio_pending(ctx) || (timerlistgroup_deadline_ns(&ctx->tlg) == 0);
}

static gboolean
aio_ctx_dispatch(GSource     *source,
                 GSourceFunc  callback,
                 gpointer     user_data)
{
    AioContext *ctx = (AioContext *) source;

    assert(callback == NULL);
    aio_dispatch(ctx);
    return true;
}

static void
aio_ctx_finalize(GSource     *source)
{
    AioContext *ctx = (AioContext *) source;

    thread_pool_free(ctx->thread_pool);

#ifdef CONFIG_LINUX_AIO
    if (ctx->linux_aio) {
        laio_detach_aio_context(ctx->linux_aio, ctx);
        laio_cleanup(ctx->linux_aio);
        ctx->linux_aio = NULL;
    }
#endif

    assert(QSLIST_EMPTY(&ctx->scheduled_coroutines));
    qemu_bh_delete(ctx->co_schedule_bh);

    qemu_lockcnt_lock(&ctx->list_lock);
    assert(!qemu_lockcnt_count(&ctx->list_lock));
    while (ctx->first_bh) {
        QEMUBH *next = ctx->first_bh->next;

        /* qemu_bh_delete() must have been called on BHs in this AioContext */
        assert(ctx->first_bh->deleted);

        g_free(ctx->first_bh);
        ctx->first_bh = next;
    }
    qemu_lockcnt_unlock(&ctx->list_lock);

    aio_set_event_notifier(ctx, &ctx->notifier, false, NULL, NULL);
    event_notifier_cleanup(&ctx->notifier);
    qemu_rec_mutex_destroy(&ctx->lock);
    qemu_lockcnt_destroy(&ctx->list_lock);
    timerlistgroup_deinit(&ctx->tlg);
    aio_context_destroy(ctx);
}

static GSourceFuncs aio_source_funcs = {
    aio_ctx_prepare,
    aio_ctx_check,
    aio_ctx_dispatch,
    aio_ctx_finalize
};

GSource *aio_get_g_source(AioContext *ctx)
{
    g_source_ref(&ctx->source);
    return &ctx->source;
}

ThreadPool *aio_get_thread_pool(AioContext *ctx)
{
    if (!ctx->thread_pool) {
        ctx->thread_pool = thread_pool_new(ctx);
    }
    return ctx->thread_pool;
}

#ifdef CONFIG_LINUX_AIO
LinuxAioState *aio_setup_linux_aio(AioContext *ctx, Error **errp)
{
    if (!ctx->linux_aio) {
        ctx->linux_aio = laio_init(errp);
        if (ctx->linux_aio) {
            laio_attach_aio_context(ctx->linux_aio, ctx);
        }
    }
    return ctx->linux_aio;
}

LinuxAioState *aio_get_linux_aio(AioContext *ctx)
{
    assert(ctx->linux_aio);
    return ctx->linux_aio;
}
#endif

void aio_notify(AioContext *ctx)
{
    /* Write e.g. bh->scheduled before reading ctx->notify_me.  Pairs
     * with atomic_or in aio_ctx_prepare or atomic_add in aio_poll.
     */
    smp_mb();
    if (ctx->notify_me) {
        event_notifier_set(&ctx->notifier);
        atomic_mb_set(&ctx->notified, true);
    }
}

void aio_notify_accept(AioContext *ctx)
{
    if (atomic_xchg(&ctx->notified, false)) {
        event_notifier_test_and_clear(&ctx->notifier);
    }
}

static void aio_timerlist_notify(void *opaque, QEMUClockType type)
{
    aio_notify(opaque);
}

static void event_notifier_dummy_cb(EventNotifier *e)
{
}

/* Returns true if aio_notify() was called (e.g. a BH was scheduled) */
static bool event_notifier_poll(void *opaque)
{
    EventNotifier *e = opaque;
    AioContext *ctx = container_of(e, AioContext, notifier);

    return atomic_read(&ctx->notified);
}

static void co_schedule_bh_cb(void *opaque)
{
    AioContext *ctx = opaque;
    QSLIST_HEAD(, Coroutine) straight, reversed;

    QSLIST_MOVE_ATOMIC(&reversed, &ctx->scheduled_coroutines);
    QSLIST_INIT(&straight);

    while (!QSLIST_EMPTY(&reversed)) {
        Coroutine *co = QSLIST_FIRST(&reversed);
        QSLIST_REMOVE_HEAD(&reversed, co_scheduled_next);
        QSLIST_INSERT_HEAD(&straight, co, co_scheduled_next);
    }

    while (!QSLIST_EMPTY(&straight)) {
        Coroutine *co = QSLIST_FIRST(&straight);
        QSLIST_REMOVE_HEAD(&straight, co_scheduled_next);
        trace_aio_co_schedule_bh_cb(ctx, co);
        aio_context_acquire(ctx);

        /* Protected by write barrier in qemu_aio_coroutine_enter */
        atomic_set(&co->scheduled, NULL);
        qemu_aio_coroutine_enter(ctx, co);
        aio_context_release(ctx);
    }
}

AioContext *aio_context_new(Error **errp)
{
    int ret;
    AioContext *ctx;

    ctx = (AioContext *) g_source_new(&aio_source_funcs, sizeof(AioContext));
    aio_context_setup(ctx);

    ret = event_notifier_init(&ctx->notifier, false);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to initialize event notifier");
        goto fail;
    }
    g_source_set_can_recurse(&ctx->source, true);
    qemu_lockcnt_init(&ctx->list_lock);

    ctx->co_schedule_bh = aio_bh_new(ctx, co_schedule_bh_cb, ctx);
    QSLIST_INIT(&ctx->scheduled_coroutines);

    aio_set_event_notifier(ctx, &ctx->notifier,
                           false,
                           (EventNotifierHandler *)
                           event_notifier_dummy_cb,
                           event_notifier_poll);
#ifdef CONFIG_LINUX_AIO
    ctx->linux_aio = NULL;
#endif
    ctx->thread_pool = NULL;
    qemu_rec_mutex_init(&ctx->lock);
    timerlistgroup_init(&ctx->tlg, aio_timerlist_notify, ctx);

    ctx->poll_ns = 0;
    ctx->poll_max_ns = 0;
    ctx->poll_grow = 0;
    ctx->poll_shrink = 0;

    return ctx;
fail:
    g_source_destroy(&ctx->source);
    return NULL;
}

void aio_co_schedule(AioContext *ctx, Coroutine *co)
{
    trace_aio_co_schedule(ctx, co);
    const char *scheduled = atomic_cmpxchg(&co->scheduled, NULL,
                                           __func__);

    if (scheduled) {
        fprintf(stderr,
                "%s: Co-routine was already scheduled in '%s'\n",
                __func__, scheduled);
        abort();
    }

    QSLIST_INSERT_HEAD_ATOMIC(&ctx->scheduled_coroutines,
                              co, co_scheduled_next);
    qemu_bh_schedule(ctx->co_schedule_bh);
}

void aio_co_wake(struct Coroutine *co)
{
    AioContext *ctx;

    /* Read coroutine before co->ctx.  Matches smp_wmb in
     * qemu_coroutine_enter.
     */
    smp_read_barrier_depends();
    ctx = atomic_read(&co->ctx);

    aio_co_enter(ctx, co);
}

void aio_co_enter(AioContext *ctx, struct Coroutine *co)
{
    if (ctx != qemu_get_current_aio_context()) {
        aio_co_schedule(ctx, co);
        return;
    }

    if (qemu_in_coroutine()) {
        Coroutine *self = qemu_coroutine_self();
        assert(self != co);
        QSIMPLEQ_INSERT_TAIL(&self->co_queue_wakeup, co, co_queue_next);
    } else {
        aio_context_acquire(ctx);
        qemu_aio_coroutine_enter(ctx, co);
        aio_context_release(ctx);
    }
}

void aio_context_ref(AioContext *ctx)
{
    g_source_ref(&ctx->source);
}

void aio_context_unref(AioContext *ctx)
{
    g_source_unref(&ctx->source);
}

void aio_context_acquire(AioContext *ctx)
{
    qemu_rec_mutex_lock(&ctx->lock);
}

void aio_context_release(AioContext *ctx)
{
    qemu_rec_mutex_unlock(&ctx->lock);
}
