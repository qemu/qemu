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
#include "qemu-aio.h"
#include "main-loop.h"

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
    bh->next = ctx->first_bh;
    ctx->first_bh = bh;
    return bh;
}

int aio_bh_poll(AioContext *ctx)
{
    QEMUBH *bh, **bhp, *next;
    int ret;

    ctx->walking_bh++;

    ret = 0;
    for (bh = ctx->first_bh; bh; bh = next) {
        next = bh->next;
        if (!bh->deleted && bh->scheduled) {
            bh->scheduled = 0;
            if (!bh->idle)
                ret = 1;
            bh->idle = 0;
            bh->cb(bh->opaque);
        }
    }

    ctx->walking_bh--;

    /* remove deleted bhs */
    if (!ctx->walking_bh) {
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
    }

    return ret;
}

void qemu_bh_schedule_idle(QEMUBH *bh)
{
    if (bh->scheduled)
        return;
    bh->scheduled = 1;
    bh->idle = 1;
}

void qemu_bh_schedule(QEMUBH *bh)
{
    if (bh->scheduled)
        return;
    bh->scheduled = 1;
    bh->idle = 0;
    aio_notify(bh->ctx);
}

void qemu_bh_cancel(QEMUBH *bh)
{
    bh->scheduled = 0;
}

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
    return aio_pending(ctx);
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

    aio_set_event_notifier(ctx, &ctx->notifier, NULL, NULL);
    event_notifier_cleanup(&ctx->notifier);
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

void aio_notify(AioContext *ctx)
{
    event_notifier_set(&ctx->notifier);
}

AioContext *aio_context_new(void)
{
    AioContext *ctx;
    ctx = (AioContext *) g_source_new(&aio_source_funcs, sizeof(AioContext));
    event_notifier_init(&ctx->notifier, false);
    aio_set_event_notifier(ctx, &ctx->notifier, 
                           (EventNotifierHandler *)
                           event_notifier_test_and_clear, NULL);

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

void aio_flush(AioContext *ctx)
{
    while (aio_poll(ctx, true));
}
