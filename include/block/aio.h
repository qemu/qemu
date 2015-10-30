/*
 * QEMU aio implementation
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_AIO_H
#define QEMU_AIO_H

#include "qemu/typedefs.h"
#include "qemu-common.h"
#include "qemu/queue.h"
#include "qemu/event_notifier.h"
#include "qemu/thread.h"
#include "qemu/rfifolock.h"
#include "qemu/timer.h"

typedef struct BlockAIOCB BlockAIOCB;
typedef void BlockCompletionFunc(void *opaque, int ret);

typedef struct AIOCBInfo {
    void (*cancel_async)(BlockAIOCB *acb);
    AioContext *(*get_aio_context)(BlockAIOCB *acb);
    size_t aiocb_size;
} AIOCBInfo;

struct BlockAIOCB {
    const AIOCBInfo *aiocb_info;
    BlockDriverState *bs;
    BlockCompletionFunc *cb;
    void *opaque;
    int refcnt;
};

void *qemu_aio_get(const AIOCBInfo *aiocb_info, BlockDriverState *bs,
                   BlockCompletionFunc *cb, void *opaque);
void qemu_aio_unref(void *p);
void qemu_aio_ref(void *p);

typedef struct AioHandler AioHandler;
typedef void QEMUBHFunc(void *opaque);
typedef void IOHandler(void *opaque);

struct AioContext {
    GSource source;

    /* Protects all fields from multi-threaded access */
    RFifoLock lock;

    /* The list of registered AIO handlers */
    QLIST_HEAD(, AioHandler) aio_handlers;

    /* This is a simple lock used to protect the aio_handlers list.
     * Specifically, it's used to ensure that no callbacks are removed while
     * we're walking and dispatching callbacks.
     */
    int walking_handlers;

    /* Used to avoid unnecessary event_notifier_set calls in aio_notify;
     * accessed with atomic primitives.  If this field is 0, everything
     * (file descriptors, bottom halves, timers) will be re-evaluated
     * before the next blocking poll(), thus the event_notifier_set call
     * can be skipped.  If it is non-zero, you may need to wake up a
     * concurrent aio_poll or the glib main event loop, making
     * event_notifier_set necessary.
     *
     * Bit 0 is reserved for GSource usage of the AioContext, and is 1
     * between a call to aio_ctx_check and the next call to aio_ctx_dispatch.
     * Bits 1-31 simply count the number of active calls to aio_poll
     * that are in the prepare or poll phase.
     *
     * The GSource and aio_poll must use a different mechanism because
     * there is no certainty that a call to GSource's prepare callback
     * (via g_main_context_prepare) is indeed followed by check and
     * dispatch.  It's not clear whether this would be a bug, but let's
     * play safe and allow it---it will just cause extra calls to
     * event_notifier_set until the next call to dispatch.
     *
     * Instead, the aio_poll calls include both the prepare and the
     * dispatch phase, hence a simple counter is enough for them.
     */
    uint32_t notify_me;

    /* lock to protect between bh's adders and deleter */
    QemuMutex bh_lock;

    /* Anchor of the list of Bottom Halves belonging to the context */
    struct QEMUBH *first_bh;

    /* A simple lock used to protect the first_bh list, and ensure that
     * no callbacks are removed while we're walking and dispatching callbacks.
     */
    int walking_bh;

    /* Used by aio_notify.
     *
     * "notified" is used to avoid expensive event_notifier_test_and_clear
     * calls.  When it is clear, the EventNotifier is clear, or one thread
     * is going to clear "notified" before processing more events.  False
     * positives are possible, i.e. "notified" could be set even though the
     * EventNotifier is clear.
     *
     * Note that event_notifier_set *cannot* be optimized the same way.  For
     * more information on the problem that would result, see "#ifdef BUG2"
     * in the docs/aio_notify_accept.promela formal model.
     */
    bool notified;
    EventNotifier notifier;

    /* Scheduling this BH forces the event loop it iterate */
    QEMUBH *notify_dummy_bh;

    /* Thread pool for performing work and receiving completion callbacks */
    struct ThreadPool *thread_pool;

    /* TimerLists for calling timers - one per clock type */
    QEMUTimerListGroup tlg;

    int external_disable_cnt;

    /* epoll(7) state used when built with CONFIG_EPOLL */
    int epollfd;
    bool epoll_enabled;
    bool epoll_available;
};

/**
 * aio_context_new: Allocate a new AioContext.
 *
 * AioContext provide a mini event-loop that can be waited on synchronously.
 * They also provide bottom halves, a service to execute a piece of code
 * as soon as possible.
 */
AioContext *aio_context_new(Error **errp);

/**
 * aio_context_ref:
 * @ctx: The AioContext to operate on.
 *
 * Add a reference to an AioContext.
 */
void aio_context_ref(AioContext *ctx);

/**
 * aio_context_unref:
 * @ctx: The AioContext to operate on.
 *
 * Drop a reference to an AioContext.
 */
void aio_context_unref(AioContext *ctx);

/* Take ownership of the AioContext.  If the AioContext will be shared between
 * threads, and a thread does not want to be interrupted, it will have to
 * take ownership around calls to aio_poll().  Otherwise, aio_poll()
 * automatically takes care of calling aio_context_acquire and
 * aio_context_release.
 *
 * Access to timers and BHs from a thread that has not acquired AioContext
 * is possible.  Access to callbacks for now must be done while the AioContext
 * is owned by the thread (FIXME).
 */
void aio_context_acquire(AioContext *ctx);

/* Relinquish ownership of the AioContext. */
void aio_context_release(AioContext *ctx);

/**
 * aio_bh_new: Allocate a new bottom half structure.
 *
 * Bottom halves are lightweight callbacks whose invocation is guaranteed
 * to be wait-free, thread-safe and signal-safe.  The #QEMUBH structure
 * is opaque and must be allocated prior to its use.
 */
QEMUBH *aio_bh_new(AioContext *ctx, QEMUBHFunc *cb, void *opaque);

/**
 * aio_notify: Force processing of pending events.
 *
 * Similar to signaling a condition variable, aio_notify forces
 * aio_wait to exit, so that the next call will re-examine pending events.
 * The caller of aio_notify will usually call aio_wait again very soon,
 * or go through another iteration of the GLib main loop.  Hence, aio_notify
 * also has the side effect of recalculating the sets of file descriptors
 * that the main loop waits for.
 *
 * Calling aio_notify is rarely necessary, because for example scheduling
 * a bottom half calls it already.
 */
void aio_notify(AioContext *ctx);

/**
 * aio_notify_accept: Acknowledge receiving an aio_notify.
 *
 * aio_notify() uses an EventNotifier in order to wake up a sleeping
 * aio_poll() or g_main_context_iteration().  Calls to aio_notify() are
 * usually rare, but the AioContext has to clear the EventNotifier on
 * every aio_poll() or g_main_context_iteration() in order to avoid
 * busy waiting.  This event_notifier_test_and_clear() cannot be done
 * using the usual aio_context_set_event_notifier(), because it must
 * be done before processing all events (file descriptors, bottom halves,
 * timers).
 *
 * aio_notify_accept() is an optimized event_notifier_test_and_clear()
 * that is specific to an AioContext's notifier; it is used internally
 * to clear the EventNotifier only if aio_notify() had been called.
 */
void aio_notify_accept(AioContext *ctx);

/**
 * aio_bh_call: Executes callback function of the specified BH.
 */
void aio_bh_call(QEMUBH *bh);

/**
 * aio_bh_poll: Poll bottom halves for an AioContext.
 *
 * These are internal functions used by the QEMU main loop.
 * And notice that multiple occurrences of aio_bh_poll cannot
 * be called concurrently
 */
int aio_bh_poll(AioContext *ctx);

/**
 * qemu_bh_schedule: Schedule a bottom half.
 *
 * Scheduling a bottom half interrupts the main loop and causes the
 * execution of the callback that was passed to qemu_bh_new.
 *
 * Bottom halves that are scheduled from a bottom half handler are instantly
 * invoked.  This can create an infinite loop if a bottom half handler
 * schedules itself.
 *
 * @bh: The bottom half to be scheduled.
 */
void qemu_bh_schedule(QEMUBH *bh);

/**
 * qemu_bh_cancel: Cancel execution of a bottom half.
 *
 * Canceling execution of a bottom half undoes the effect of calls to
 * qemu_bh_schedule without freeing its resources yet.  While cancellation
 * itself is also wait-free and thread-safe, it can of course race with the
 * loop that executes bottom halves unless you are holding the iothread
 * mutex.  This makes it mostly useless if you are not holding the mutex.
 *
 * @bh: The bottom half to be canceled.
 */
void qemu_bh_cancel(QEMUBH *bh);

/**
 *qemu_bh_delete: Cancel execution of a bottom half and free its resources.
 *
 * Deleting a bottom half frees the memory that was allocated for it by
 * qemu_bh_new.  It also implies canceling the bottom half if it was
 * scheduled.
 * This func is async. The bottom half will do the delete action at the finial
 * end.
 *
 * @bh: The bottom half to be deleted.
 */
void qemu_bh_delete(QEMUBH *bh);

/* Return whether there are any pending callbacks from the GSource
 * attached to the AioContext, before g_poll is invoked.
 *
 * This is used internally in the implementation of the GSource.
 */
bool aio_prepare(AioContext *ctx);

/* Return whether there are any pending callbacks from the GSource
 * attached to the AioContext, after g_poll is invoked.
 *
 * This is used internally in the implementation of the GSource.
 */
bool aio_pending(AioContext *ctx);

/* Dispatch any pending callbacks from the GSource attached to the AioContext.
 *
 * This is used internally in the implementation of the GSource.
 */
bool aio_dispatch(AioContext *ctx);

/* Progress in completing AIO work to occur.  This can issue new pending
 * aio as a result of executing I/O completion or bh callbacks.
 *
 * Return whether any progress was made by executing AIO or bottom half
 * handlers.  If @blocking == true, this should always be true except
 * if someone called aio_notify.
 *
 * If there are no pending bottom halves, but there are pending AIO
 * operations, it may not be possible to make any progress without
 * blocking.  If @blocking is true, this function will wait until one
 * or more AIO events have completed, to ensure something has moved
 * before returning.
 */
bool aio_poll(AioContext *ctx, bool blocking);

/* Register a file descriptor and associated callbacks.  Behaves very similarly
 * to qemu_set_fd_handler.  Unlike qemu_set_fd_handler, these callbacks will
 * be invoked when using aio_poll().
 *
 * Code that invokes AIO completion functions should rely on this function
 * instead of qemu_set_fd_handler[2].
 */
void aio_set_fd_handler(AioContext *ctx,
                        int fd,
                        bool is_external,
                        IOHandler *io_read,
                        IOHandler *io_write,
                        void *opaque);

/* Register an event notifier and associated callbacks.  Behaves very similarly
 * to event_notifier_set_handler.  Unlike event_notifier_set_handler, these callbacks
 * will be invoked when using aio_poll().
 *
 * Code that invokes AIO completion functions should rely on this function
 * instead of event_notifier_set_handler.
 */
void aio_set_event_notifier(AioContext *ctx,
                            EventNotifier *notifier,
                            bool is_external,
                            EventNotifierHandler *io_read);

/* Return a GSource that lets the main loop poll the file descriptors attached
 * to this AioContext.
 */
GSource *aio_get_g_source(AioContext *ctx);

/* Return the ThreadPool bound to this AioContext */
struct ThreadPool *aio_get_thread_pool(AioContext *ctx);

/**
 * aio_timer_new:
 * @ctx: the aio context
 * @type: the clock type
 * @scale: the scale
 * @cb: the callback to call on timer expiry
 * @opaque: the opaque pointer to pass to the callback
 *
 * Allocate a new timer attached to the context @ctx.
 * The function is responsible for memory allocation.
 *
 * The preferred interface is aio_timer_init. Use that
 * unless you really need dynamic memory allocation.
 *
 * Returns: a pointer to the new timer
 */
static inline QEMUTimer *aio_timer_new(AioContext *ctx, QEMUClockType type,
                                       int scale,
                                       QEMUTimerCB *cb, void *opaque)
{
    return timer_new_tl(ctx->tlg.tl[type], scale, cb, opaque);
}

/**
 * aio_timer_init:
 * @ctx: the aio context
 * @ts: the timer
 * @type: the clock type
 * @scale: the scale
 * @cb: the callback to call on timer expiry
 * @opaque: the opaque pointer to pass to the callback
 *
 * Initialise a new timer attached to the context @ctx.
 * The caller is responsible for memory allocation.
 */
static inline void aio_timer_init(AioContext *ctx,
                                  QEMUTimer *ts, QEMUClockType type,
                                  int scale,
                                  QEMUTimerCB *cb, void *opaque)
{
    timer_init_tl(ts, ctx->tlg.tl[type], scale, cb, opaque);
}

/**
 * aio_compute_timeout:
 * @ctx: the aio context
 *
 * Compute the timeout that a blocking aio_poll should use.
 */
int64_t aio_compute_timeout(AioContext *ctx);

/**
 * aio_disable_external:
 * @ctx: the aio context
 *
 * Disable the further processing of external clients.
 */
static inline void aio_disable_external(AioContext *ctx)
{
    atomic_inc(&ctx->external_disable_cnt);
}

/**
 * aio_enable_external:
 * @ctx: the aio context
 *
 * Enable the processing of external clients.
 */
static inline void aio_enable_external(AioContext *ctx)
{
    assert(ctx->external_disable_cnt > 0);
    atomic_dec(&ctx->external_disable_cnt);
}

/**
 * aio_external_disabled:
 * @ctx: the aio context
 *
 * Return true if the external clients are disabled.
 */
static inline bool aio_external_disabled(AioContext *ctx)
{
    return atomic_read(&ctx->external_disable_cnt);
}

/**
 * aio_node_check:
 * @ctx: the aio context
 * @is_external: Whether or not the checked node is an external event source.
 *
 * Check if the node's is_external flag is okay to be polled by the ctx at this
 * moment. True means green light.
 */
static inline bool aio_node_check(AioContext *ctx, bool is_external)
{
    return !is_external || !atomic_read(&ctx->external_disable_cnt);
}

/**
 * aio_context_setup:
 * @ctx: the aio context
 *
 * Initialize the aio context.
 */
void aio_context_setup(AioContext *ctx, Error **errp);

#endif
