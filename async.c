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

/*
 * An AsyncContext protects the callbacks of AIO requests and Bottom Halves
 * against interfering with each other. A typical example is qcow2 that accepts
 * asynchronous requests, but relies for manipulation of its metadata on
 * synchronous bdrv_read/write that doesn't trigger any callbacks.
 *
 * However, these functions are often emulated using AIO which means that AIO
 * callbacks must be run - but at the same time we must not run callbacks of
 * other requests as they might start to modify metadata and corrupt the
 * internal state of the caller of bdrv_read/write.
 *
 * To achieve the desired semantics we switch into a new AsyncContext.
 * Callbacks must only be run if they belong to the current AsyncContext.
 * Otherwise they need to be queued until their own context is active again.
 * This is how you can make qemu_aio_wait() wait only for your own callbacks.
 *
 * The AsyncContexts form a stack. When you leave a AsyncContexts, you always
 * return to the old ("parent") context.
 */
struct AsyncContext {
    /* Consecutive number of the AsyncContext (position in the stack) */
    int id;

    /* Anchor of the list of Bottom Halves belonging to the context */
    struct QEMUBH *first_bh;

    /* Link to parent context */
    struct AsyncContext *parent;
};

/* The currently active AsyncContext */
static struct AsyncContext *async_context = &(struct AsyncContext) { 0 };

/*
 * Enter a new AsyncContext. Already scheduled Bottom Halves and AIO callbacks
 * won't be called until this context is left again.
 */
void async_context_push(void)
{
    struct AsyncContext *new = qemu_mallocz(sizeof(*new));
    new->parent = async_context;
    new->id = async_context->id + 1;
    async_context = new;
}

/* Run queued AIO completions and destroy Bottom Half */
static void bh_run_aio_completions(void *opaque)
{
    QEMUBH **bh = opaque;
    qemu_bh_delete(*bh);
    qemu_free(bh);
    qemu_aio_process_queue();
}
/*
 * Leave the currently active AsyncContext. All Bottom Halves belonging to the
 * old context are executed before changing the context.
 */
void async_context_pop(void)
{
    struct AsyncContext *old = async_context;
    QEMUBH **bh;

    /* Flush the bottom halves, we don't want to lose them */
    while (qemu_bh_poll());

    /* Switch back to the parent context */
    async_context = async_context->parent;
    qemu_free(old);

    if (async_context == NULL) {
        abort();
    }

    /* Schedule BH to run any queued AIO completions as soon as possible */
    bh = qemu_malloc(sizeof(*bh));
    *bh = qemu_bh_new(bh_run_aio_completions, bh);
    qemu_bh_schedule(*bh);
}

/*
 * Returns the ID of the currently active AsyncContext
 */
int get_async_context_id(void)
{
    return async_context->id;
}

/***********************************************************/
/* bottom halves (can be seen as timers which expire ASAP) */

struct QEMUBH {
    QEMUBHFunc *cb;
    void *opaque;
    int scheduled;
    int idle;
    int deleted;
    QEMUBH *next;
};

QEMUBH *qemu_bh_new(QEMUBHFunc *cb, void *opaque)
{
    QEMUBH *bh;
    bh = qemu_mallocz(sizeof(QEMUBH));
    bh->cb = cb;
    bh->opaque = opaque;
    bh->next = async_context->first_bh;
    async_context->first_bh = bh;
    return bh;
}

int qemu_bh_poll(void)
{
    QEMUBH *bh, **bhp;
    int ret;

    ret = 0;
    for (bh = async_context->first_bh; bh; bh = bh->next) {
        if (!bh->deleted && bh->scheduled) {
            bh->scheduled = 0;
            if (!bh->idle)
                ret = 1;
            bh->idle = 0;
            bh->cb(bh->opaque);
        }
    }

    /* remove deleted bhs */
    bhp = &async_context->first_bh;
    while (*bhp) {
        bh = *bhp;
        if (bh->deleted) {
            *bhp = bh->next;
            qemu_free(bh);
        } else
            bhp = &bh->next;
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
    /* stop the currently executing CPU to execute the BH ASAP */
    qemu_notify_event();
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

void qemu_bh_update_timeout(int *timeout)
{
    QEMUBH *bh;

    for (bh = async_context->first_bh; bh; bh = bh->next) {
        if (!bh->deleted && bh->scheduled) {
            if (bh->idle) {
                /* idle bottom halves will be polled at least
                 * every 10ms */
                *timeout = MIN(10, *timeout);
            } else {
                /* non-idle bottom halves will be executed
                 * immediately */
                *timeout = 0;
                break;
            }
        }
    }
}

