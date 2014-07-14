/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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

#include "qemu-common.h"
#include "block/aio.h"
#include "block/thread-pool.h"
#include "qemu/main-loop.h"
#include "qemu/atomic.h"

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

QEMUBH *aio_bh_new(AioContext *ctx, QEMUBHFunc *cb, void *opaque)
{
    QEMUBH *bh;
    bh = g_malloc0(sizeof(QEMUBH));
    bh->ctx = ctx;
    bh->cb = cb;
    bh->opaque = opaque;
    qemu_mutex_lock(&ctx->bh_lock);
    bh->next = ctx->first_bh;
    /* Make sure that the members are ready before putting bh into list */
    smp_wmb();
    ctx->first_bh = bh;
    qemu_mutex_unlock(&ctx->bh_lock);
    return bh;
}

/* Multiple occurrences of aio_bh_poll cannot be called concurrently */
int aio_bh_poll(AioContext *ctx)
{
    QEMUBH *bh, **bhp, *next;
    int ret;

    ctx->walking_bh++;

    ret = 0;
    for (bh = ctx->first_bh; bh; bh = next) {
        /* Make sure that fetching bh happens before accessing its members */
        smp_read_barrier_depends();
        next = bh->next;
        if (!bh->deleted && bh->scheduled) {
            bh->scheduled = 0;
            /* Paired with write barrier in bh schedule to ensure reading for
             * idle & callbacks coming after bh's scheduling.
             */
            smp_rmb();
            if (!bh->idle)
                ret = 1;
            bh->idle = 0;
            bh->cb(bh->opaque);
        }
    }

    ctx->walking_bh--;

    /* remove deleted bhs */
    if (!ctx->walking_bh) {
        qemu_mutex_lock(&ctx->bh_lock);
        bhp = &ctx->first_bh;
        while (*bhp) {
            bh = *bhp;
            if (bh->deleted) {
                *bhp = bh->next;
                g_free(bh);
            } else {
                bhp = &bh->next;
            }
        }
        qemu_mutex_unlock(&ctx->bh_lock);
    }

    return ret;
}

void qemu_bh_schedule_idle(QEMUBH *bh)
{
    if (bh->scheduled)
        return;
    bh->idle = 1;
    /* Make sure that idle & any writes needed by the callback are done
     * before the locations are read in the aio_bh_poll.
     */
    smp_wmb();
    bh->scheduled = 1;
}

void qemu_bh_schedule(QEMUBH *bh)
{
    AioContext *ctx;

    if (bh->scheduled)
        return;
    ctx = bh->ctx;
    bh->idle = 0;
    /* Make sure that:
     * 1. idle & any writes needed by the callback are done before the
     *    locations are read in the aio_bh_poll.
     * 2. ctx is loaded before scheduled is set and the callback has a chance
     *    to execute.
     */
    smp_mb();
    bh->scheduled = 1;
    aio_notify(ctx);
}


/* This func is async.
 */
void qemu_bh_cancel(QEMUBH *bh)
{
    bh->scheduled = 0;
}

/* This func is async.The bottom half will do the delete action at the finial
 * end.
 */
void qemu_bh_delete(QEMUBH *bh)
{
    bh->scheduled = 0;
    bh->deleted = 1;
}

static gboolean
aio_ctx_prepare(GSource *source, gint    *timeout)
{
    AioContext *ctx = (AioContext *) source;
    QEMUBH *bh;
    int deadline;

    /* We assume there is no timeout already supplied */
    *timeout = -1;
    for (bh = ctx->first_bh; bh; bh = bh->next) {
        if (!bh->deleted && bh->scheduled) {
            if (bh->idle) {
                /* idle bottom halves will be polled at least
                 * every 10ms */
                *timeout = 10;
            } else {
                /* non-idle bottom halves will be executed
                 * immediately */
                *timeout = 0;
                return true;
            }
        }
    }

    deadline = qemu_timeout_ns_to_ms(timerlistgroup_deadline_ns(&ctx->tlg));
    if (deadline == 0) {
        *timeout = 0;
        return true;
    } else {
        *timeout = qemu_soonest_timeout(*timeout, deadline);
    }

    return false;
}

static gboolean
aio_ctx_check(GSource *source)
{
    AioContext *ctx = (AioContext *) source;
    QEMUBH *bh;

    for (bh = ctx->first_bh; bh; bh = bh->next) {
        if (!bh->deleted && bh->scheduled) {
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
    aio_poll(ctx, false);
    return true;
}

static void
aio_ctx_finalize(GSource     *source)
{
    AioContext *ctx = (AioContext *) source;

    thread_pool_free(ctx->thread_pool);
    aio_set_event_notifier(ctx, &ctx->notifier, NULL);
    event_notifier_cleanup(&ctx->notifier);
    rfifolock_destroy(&ctx->lock);
    qemu_mutex_destroy(&ctx->bh_lock);
    g_array_free(ctx->pollfds, TRUE);
    timerlistgroup_deinit(&ctx->tlg);
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

void aio_set_dispatching(AioContext *ctx, bool dispatching)
{
    ctx->dispatching = dispatching;
    if (!dispatching) {
        /* Write ctx->dispatching before reading e.g. bh->scheduled.
         * Optimization: this is only needed when we're entering the "unsafe"
         * phase where other threads must call event_notifier_set.
         */
        smp_mb();
    }
}

void aio_notify(AioContext *ctx)
{
    /* Write e.g. bh->scheduled before reading ctx->dispatching.  */
    smp_mb();
    if (!ctx->dispatching) {
        event_notifier_set(&ctx->notifier);
    }
}

static void aio_timerlist_notify(void *opaque)
{
    aio_notify(opaque);
}

static void aio_rfifolock_cb(void *opaque)
{
    /* Kick owner thread in case they are blocked in aio_poll() */
    aio_notify(opaque);
}

AioContext *aio_context_new(void)
{
    AioContext *ctx;
    ctx = (AioContext *) g_source_new(&aio_source_funcs, sizeof(AioContext));
    ctx->pollfds = g_array_new(FALSE, FALSE, sizeof(GPollFD));
    ctx->thread_pool = NULL;
    qemu_mutex_init(&ctx->bh_lock);
    rfifolock_init(&ctx->lock, aio_rfifolock_cb, ctx);
    event_notifier_init(&ctx->notifier, false);
    aio_set_event_notifier(ctx, &ctx->notifier, 
                           (EventNotifierHandler *)
                           event_notifier_test_and_clear);
    timerlistgroup_init(&ctx->tlg, aio_timerlist_notify, ctx);

    return ctx;
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
    rfifolock_lock(&ctx->lock);
}

void aio_context_release(AioContext *ctx)
{
    rfifolock_unlock(&ctx->lock);
}
