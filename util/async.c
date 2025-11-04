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
#include "block/aio.h"
#include "block/thread-pool.h"
#include "block/graph-lock.h"
#include "qemu/main-loop.h"
#include "qemu/atomic.h"
#include "qemu/lockcnt.h"
#include "qemu/rcu_queue.h"
#include "block/raw-aio.h"
#include "qemu/coroutine_int.h"
#include "qemu/coroutine-tls.h"
#include "exec/icount.h"
#include "trace.h"

/***********************************************************/
/* bottom halves (can be seen as timers which expire ASAP) */

/* QEMUBH::flags values */
enum {
    /* Already enqueued and waiting for aio_bh_poll() */
    BH_PENDING   = (1 << 0),

    /* Invoke the callback */
    BH_SCHEDULED = (1 << 1),

    /* Delete without invoking callback */
    BH_DELETED   = (1 << 2),

    /* Delete after invoking callback */
    BH_ONESHOT   = (1 << 3),

    /* Schedule periodically when the event loop is idle */
    BH_IDLE      = (1 << 4),
};

struct QEMUBH {
    AioContext *ctx;
    const char *name;
    QEMUBHFunc *cb;
    void *opaque;
    QSLIST_ENTRY(QEMUBH) next;
    unsigned flags;
    MemReentrancyGuard *reentrancy_guard;
};

/* Called concurrently from any thread */
static void aio_bh_enqueue(QEMUBH *bh, unsigned new_flags)
{
    AioContext *ctx = bh->ctx;
    unsigned old_flags;

    /*
     * Synchronizes with atomic_fetch_and() in aio_bh_dequeue(), ensuring that
     * insertion starts after BH_PENDING is set.
     */
    old_flags = qatomic_fetch_or(&bh->flags, BH_PENDING | new_flags);

    if (!(old_flags & BH_PENDING)) {
        /*
         * At this point the bottom half becomes visible to aio_bh_poll().
         * This insertion thus synchronizes with QSLIST_MOVE_ATOMIC in
         * aio_bh_poll(), ensuring that:
         * 1. any writes needed by the callback are visible from the callback
         *    after aio_bh_dequeue() returns bh.
         * 2. ctx is loaded before the callback has a chance to execute and bh
         *    could be freed.
         */
        QSLIST_INSERT_HEAD_ATOMIC(&ctx->bh_list, bh, next);
    }

    aio_notify(ctx);
    if (unlikely(icount_enabled())) {
        /*
         * Workaround for record/replay.
         * vCPU execution should be suspended when new BH is set.
         * This is needed to avoid guest timeouts caused
         * by the long cycles of the execution.
         */
        icount_notify_exit();
    }
}

/* Only called from aio_bh_poll() and aio_ctx_finalize() */
static QEMUBH *aio_bh_dequeue(BHList *head, unsigned *flags)
{
    QEMUBH *bh = QSLIST_FIRST_RCU(head);

    if (!bh) {
        return NULL;
    }

    QSLIST_REMOVE_HEAD(head, next);

    /*
     * Synchronizes with qatomic_fetch_or() in aio_bh_enqueue(), ensuring that
     * the removal finishes before BH_PENDING is reset.
     */
    *flags = qatomic_fetch_and(&bh->flags,
                              ~(BH_PENDING | BH_SCHEDULED | BH_IDLE));
    return bh;
}

void aio_bh_schedule_oneshot_full(AioContext *ctx, QEMUBHFunc *cb,
                                  void *opaque, const char *name)
{
    QEMUBH *bh;
    bh = g_new(QEMUBH, 1);
    *bh = (QEMUBH){
        .ctx = ctx,
        .cb = cb,
        .opaque = opaque,
        .name = name,
    };
    aio_bh_enqueue(bh, BH_SCHEDULED | BH_ONESHOT);
}

QEMUBH *aio_bh_new_full(AioContext *ctx, QEMUBHFunc *cb, void *opaque,
                        const char *name, MemReentrancyGuard *reentrancy_guard)
{
    QEMUBH *bh;
    bh = g_new(QEMUBH, 1);
    *bh = (QEMUBH){
        .ctx = ctx,
        .cb = cb,
        .opaque = opaque,
        .name = name,
        .reentrancy_guard = reentrancy_guard,
    };
    return bh;
}

void aio_bh_call(QEMUBH *bh)
{
    bool last_engaged_in_io = false;

    /* Make a copy of the guard-pointer as cb may free the bh */
    MemReentrancyGuard *reentrancy_guard = bh->reentrancy_guard;
    if (reentrancy_guard) {
        last_engaged_in_io = reentrancy_guard->engaged_in_io;
        if (reentrancy_guard->engaged_in_io) {
            trace_reentrant_aio(bh->ctx, bh->name);
        }
        reentrancy_guard->engaged_in_io = true;
    }

    bh->cb(bh->opaque);

    if (reentrancy_guard) {
        reentrancy_guard->engaged_in_io = last_engaged_in_io;
    }
}

/* Multiple occurrences of aio_bh_poll cannot be called concurrently. */
int aio_bh_poll(AioContext *ctx)
{
    BHListSlice slice;
    BHListSlice *s;
    int ret = 0;

    /* Synchronizes with QSLIST_INSERT_HEAD_ATOMIC in aio_bh_enqueue().  */
    QSLIST_MOVE_ATOMIC(&slice.bh_list, &ctx->bh_list);

    /*
     * GCC13 [-Werror=dangling-pointer=] complains that the local variable
     * 'slice' is being stored in the global 'ctx->bh_slice_list' but the
     * list is emptied before this function returns.
     */
#if !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wdangling-pointer="
#endif
    QSIMPLEQ_INSERT_TAIL(&ctx->bh_slice_list, &slice, next);
#if !defined(__clang__)
#pragma GCC diagnostic pop
#endif

    while ((s = QSIMPLEQ_FIRST(&ctx->bh_slice_list))) {
        QEMUBH *bh;
        unsigned flags;

        bh = aio_bh_dequeue(&s->bh_list, &flags);
        if (!bh) {
            QSIMPLEQ_REMOVE_HEAD(&ctx->bh_slice_list, next);
            continue;
        }

        if ((flags & (BH_SCHEDULED | BH_DELETED)) == BH_SCHEDULED) {
            /* Idle BHs don't count as progress */
            if (!(flags & BH_IDLE)) {
                ret = 1;
            }
            aio_bh_call(bh);
        }
        if (flags & (BH_DELETED | BH_ONESHOT)) {
            g_free(bh);
        }
    }

    return ret;
}

void qemu_bh_schedule_idle(QEMUBH *bh)
{
    aio_bh_enqueue(bh, BH_SCHEDULED | BH_IDLE);
}

void qemu_bh_schedule(QEMUBH *bh)
{
    aio_bh_enqueue(bh, BH_SCHEDULED);
}

/* This func is async.
 */
void qemu_bh_cancel(QEMUBH *bh)
{
    qatomic_and(&bh->flags, ~BH_SCHEDULED);
}

/* This func is async.The bottom half will do the delete action at the finial
 * end.
 */
void qemu_bh_delete(QEMUBH *bh)
{
    aio_bh_enqueue(bh, BH_DELETED);
}

static int64_t aio_compute_bh_timeout(BHList *head, int timeout)
{
    QEMUBH *bh;

    QSLIST_FOREACH_RCU(bh, head, next) {
        int flags = qatomic_load_acquire(&bh->flags);
        if ((flags & (BH_SCHEDULED | BH_DELETED)) == BH_SCHEDULED) {
            if (flags & BH_IDLE) {
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

    return timeout;
}

int64_t
aio_compute_timeout(AioContext *ctx)
{
    BHListSlice *s;
    int64_t deadline;
    int timeout = -1;

    timeout = aio_compute_bh_timeout(&ctx->bh_list, timeout);
    if (timeout == 0) {
        return 0;
    }

    QSIMPLEQ_FOREACH(s, &ctx->bh_slice_list, next) {
        timeout = aio_compute_bh_timeout(&s->bh_list, timeout);
        if (timeout == 0) {
            return 0;
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

    qatomic_set(&ctx->notify_me, qatomic_read(&ctx->notify_me) | 1);

    /*
     * Write ctx->notify_me before computing the timeout
     * (reading bottom half flags, etc.).  Pairs with
     * smp_mb in aio_notify().
     */
    smp_mb();

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
    BHListSlice *s;

    /* Finish computing the timeout before clearing the flag.  */
    qatomic_store_release(&ctx->notify_me, qatomic_read(&ctx->notify_me) & ~1);
    aio_notify_accept(ctx);

    QSLIST_FOREACH_RCU(bh, &ctx->bh_list, next) {
        int flags = qatomic_load_acquire(&bh->flags);
        if ((flags & (BH_SCHEDULED | BH_DELETED)) == BH_SCHEDULED) {
            return true;
        }
    }

    QSIMPLEQ_FOREACH(s, &ctx->bh_slice_list, next) {
        QSLIST_FOREACH_RCU(bh, &s->bh_list, next) {
            int flags = qatomic_load_acquire(&bh->flags);
            if ((flags & (BH_SCHEDULED | BH_DELETED)) == BH_SCHEDULED) {
                return true;
            }
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
aio_ctx_finalize(GSource *source)
{
    AioContext *ctx = (AioContext *) source;
    QEMUBH *bh;
    unsigned flags;

    if (!ctx->initialized) {
        return;
    }

    thread_pool_free_aio(ctx->thread_pool);

#ifdef CONFIG_LINUX_AIO
    if (ctx->linux_aio) {
        laio_detach_aio_context(ctx->linux_aio, ctx);
        laio_cleanup(ctx->linux_aio);
        ctx->linux_aio = NULL;
    }
#endif

    assert(QSLIST_EMPTY(&ctx->scheduled_coroutines));
    qemu_bh_delete(ctx->co_schedule_bh);

    /* There must be no aio_bh_poll() calls going on */
    assert(QSIMPLEQ_EMPTY(&ctx->bh_slice_list));

    while ((bh = aio_bh_dequeue(&ctx->bh_list, &flags))) {
        /*
         * qemu_bh_delete() must have been called on BHs in this AioContext. In
         * many cases memory leaks, hangs, or inconsistent state occur when a
         * BH is leaked because something still expects it to run.
         *
         * If you hit this, fix the lifecycle of the BH so that
         * qemu_bh_delete() and any associated cleanup is called before the
         * AioContext is finalized.
         */
        if (unlikely(!(flags & BH_DELETED))) {
            fprintf(stderr, "%s: BH '%s' leaked, aborting...\n",
                    __func__, bh->name);
            abort();
        }

        g_free(bh);
    }

    aio_set_event_notifier(ctx, &ctx->notifier, NULL, NULL, NULL);
    event_notifier_cleanup(&ctx->notifier);
    qemu_rec_mutex_destroy(&ctx->lock);
    timerlistgroup_deinit(&ctx->tlg);
    unregister_aiocontext(ctx);
    aio_context_destroy(ctx);
    /* aio_context_destroy() still needs the lock */
    qemu_lockcnt_destroy(&ctx->list_lock);
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

ThreadPoolAio *aio_get_thread_pool(AioContext *ctx)
{
    if (!ctx->thread_pool) {
        ctx->thread_pool = thread_pool_new_aio(ctx);
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
    /*
     * Write e.g. ctx->bh_list before writing ctx->notified.  Pairs with
     * smp_mb() in aio_notify_accept().
     */
    smp_wmb();
    qatomic_set(&ctx->notified, true);

    /*
     * Write ctx->notified (and also ctx->bh_list) before reading ctx->notify_me.
     * Pairs with smp_mb() in aio_ctx_prepare or aio_poll.
     */
    smp_mb();
    if (qatomic_read(&ctx->notify_me)) {
        event_notifier_set(&ctx->notifier);
    }
}

void aio_notify_accept(AioContext *ctx)
{
    qatomic_set(&ctx->notified, false);

    /*
     * Order reads of ctx->notified (in aio_context_notifier_poll()) and the
     * above clearing of ctx->notified before reads of e.g. bh->flags.  Pairs
     * with smp_wmb() in aio_notify.
     */
    smp_mb();
}

static void aio_timerlist_notify(void *opaque, QEMUClockType type)
{
    aio_notify(opaque);
}

static void aio_context_notifier_cb(EventNotifier *e)
{
    AioContext *ctx = container_of(e, AioContext, notifier);

    event_notifier_test_and_clear(&ctx->notifier);
}

/* Returns true if aio_notify() was called (e.g. a BH was scheduled) */
static bool aio_context_notifier_poll(void *opaque)
{
    EventNotifier *e = opaque;
    AioContext *ctx = container_of(e, AioContext, notifier);

    /*
     * No need for load-acquire because we just want to kick the
     * event loop.  aio_notify_accept() takes care of synchronizing
     * the event loop with the producers.
     */
    return qatomic_read(&ctx->notified);
}

static void aio_context_notifier_poll_ready(EventNotifier *e)
{
    /* Do nothing, we just wanted to kick the event loop */
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

        /* Protected by write barrier in qemu_aio_coroutine_enter */
        qatomic_set(&co->scheduled, NULL);
        qemu_aio_coroutine_enter(ctx, co);
    }
}

AioContext *aio_context_new(Error **errp)
{
    ERRP_GUARD();
    int ret;
    AioContext *ctx;

    /*
     * ctx is freed by g_source_unref() (e.g. aio_context_unref()). ctx's
     * resources are freed as follows:
     *
     * 1. By aio_ctx_finalize() after aio_context_new() has returned and set
     *    ->initialized = true.
     *
     * 2. By manual cleanup code in this function's error paths before goto
     *    fail.
     *
     * Be careful to free resources in both cases!
     */
    ctx = (AioContext *) g_source_new(&aio_source_funcs, sizeof(AioContext));
    QSLIST_INIT(&ctx->bh_list);
    QSIMPLEQ_INIT(&ctx->bh_slice_list);

    ret = event_notifier_init(&ctx->notifier, false);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to initialize event notifier");
        goto fail;
    }

    /*
     * Resources cannot easily be freed manually after aio_context_setup(). If
     * you add any new resources to AioContext, it's probably best to acquire
     * them before aio_context_setup().
     */
    if (!aio_context_setup(ctx, errp)) {
        event_notifier_cleanup(&ctx->notifier);
        goto fail;
    }

    g_source_set_can_recurse(&ctx->source, true);
    qemu_lockcnt_init(&ctx->list_lock);

    ctx->co_schedule_bh = aio_bh_new(ctx, co_schedule_bh_cb, ctx);
    QSLIST_INIT(&ctx->scheduled_coroutines);

    aio_set_event_notifier(ctx, &ctx->notifier,
                           aio_context_notifier_cb,
                           aio_context_notifier_poll,
                           aio_context_notifier_poll_ready);
#ifdef CONFIG_LINUX_AIO
    ctx->linux_aio = NULL;
#endif

    ctx->thread_pool = NULL;
    qemu_rec_mutex_init(&ctx->lock);
    timerlistgroup_init(&ctx->tlg, aio_timerlist_notify, ctx);

    ctx->poll_max_ns = 0;
    ctx->poll_grow = 0;
    ctx->poll_shrink = 0;

    ctx->aio_max_batch = 0;

    ctx->thread_pool_min = 0;
    ctx->thread_pool_max = THREAD_POOL_MAX_THREADS_DEFAULT;

    register_aiocontext(ctx);

    ctx->initialized = true;

    return ctx;
fail:
    g_source_unref(&ctx->source);
    return NULL;
}

void aio_co_schedule(AioContext *ctx, Coroutine *co)
{
    trace_aio_co_schedule(ctx, co);
    const char *scheduled = qatomic_cmpxchg(&co->scheduled, NULL,
                                           __func__);

    if (scheduled) {
        fprintf(stderr,
                "%s: Co-routine was already scheduled in '%s'\n",
                __func__, scheduled);
        abort();
    }

    /* The coroutine might run and release the last ctx reference before we
     * invoke qemu_bh_schedule().  Take a reference to keep ctx alive until
     * we're done.
     */
    aio_context_ref(ctx);

    QSLIST_INSERT_HEAD_ATOMIC(&ctx->scheduled_coroutines,
                              co, co_scheduled_next);
    qemu_bh_schedule(ctx->co_schedule_bh);

    aio_context_unref(ctx);
}

typedef struct AioCoRescheduleSelf {
    Coroutine *co;
    AioContext *new_ctx;
} AioCoRescheduleSelf;

static void aio_co_reschedule_self_bh(void *opaque)
{
    AioCoRescheduleSelf *data = opaque;
    aio_co_schedule(data->new_ctx, data->co);
}

void coroutine_fn aio_co_reschedule_self(AioContext *new_ctx)
{
    AioContext *old_ctx = qemu_get_current_aio_context();

    if (old_ctx != new_ctx) {
        AioCoRescheduleSelf data = {
            .co = qemu_coroutine_self(),
            .new_ctx = new_ctx,
        };
        /*
         * We can't directly schedule the coroutine in the target context
         * because this would be racy: The other thread could try to enter the
         * coroutine before it has yielded in this one.
         */
        aio_bh_schedule_oneshot(old_ctx, aio_co_reschedule_self_bh, &data);
        qemu_coroutine_yield();
    }
}

void aio_co_wake(Coroutine *co)
{
    AioContext *ctx;

    /* Read coroutine before co->ctx.  Matches smp_wmb in
     * qemu_coroutine_enter.
     */
    smp_read_barrier_depends();
    ctx = qatomic_read(&co->ctx);

    aio_co_enter(ctx, co);
}

void aio_co_enter(AioContext *ctx, Coroutine *co)
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
        qemu_aio_coroutine_enter(ctx, co);
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

QEMU_DEFINE_STATIC_CO_TLS(AioContext *, my_aiocontext)

AioContext *qemu_get_current_aio_context(void)
{
    AioContext *ctx = get_my_aiocontext();
    if (ctx) {
        return ctx;
    }
    if (bql_locked()) {
        /* Possibly in a vCPU thread.  */
        return qemu_get_aio_context();
    }
    return NULL;
}

void qemu_set_current_aio_context(AioContext *ctx)
{
    assert(!get_my_aiocontext());
    set_my_aiocontext(ctx);
}

void aio_context_set_thread_pool_params(AioContext *ctx, int64_t min,
                                        int64_t max, Error **errp)
{

    if (min > max || max <= 0 || min < 0 || min > INT_MAX || max > INT_MAX) {
        error_setg(errp, "bad thread-pool-min/thread-pool-max values");
        return;
    }

    ctx->thread_pool_min = min;
    ctx->thread_pool_max = max;

    if (ctx->thread_pool) {
        thread_pool_update_params(ctx->thread_pool, ctx);
    }
}
