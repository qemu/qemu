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

#include "qemu/queue.h"
#include "qemu/event_notifier.h"
#include "qemu/thread.h"
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
typedef bool AioPollFn(void *opaque);
typedef void IOHandler(void *opaque);

struct Coroutine;
struct ThreadPool;
struct LinuxAioState;

struct AioContext {
    GSource source;

    /* Used by AioContext users to protect from multi-threaded access.  */
    QemuRecMutex lock;

    /* The list of registered AIO handlers.  Protected by ctx->list_lock. */
    QLIST_HEAD(, AioHandler) aio_handlers;

    /* Used to avoid unnecessary event_notifier_set calls in aio_notify;
     * accessed with atomic primitives.  If this field is 0, everything
     * (file descriptors, bottom halves, timers) will be re-evaluated
     * before the next blocking poll(), thus the event_notifier_set call
     * can be skipped.  If it is non-zero, you may need to wake up a
     * concurrent aio_poll or the glib main event loop, making
     * event_notifier_set necessary.
     *
     * Bit 0 is reserved for GSource usage of the AioContext, and is 1
     * between a call to aio_ctx_prepare and the next call to aio_ctx_check.
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

    /* A lock to protect between QEMUBH and AioHandler adders and deleter,
     * and to ensure that no callbacks are removed while we're walking and
     * dispatching them.
     */
    QemuLockCnt list_lock;

    /* Anchor of the list of Bottom Halves belonging to the context */
    struct QEMUBH *first_bh;

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

    QSLIST_HEAD(, Coroutine) scheduled_coroutines;
    QEMUBH *co_schedule_bh;

    /* Thread pool for performing work and receiving completion callbacks.
     * Has its own locking.
     */
    struct ThreadPool *thread_pool;

#ifdef CONFIG_LINUX_AIO
    /* State for native Linux AIO.  Uses aio_context_acquire/release for
     * locking.
     */
    struct LinuxAioState *linux_aio;
#endif

    /* TimerLists for calling timers - one per clock type.  Has its own
     * locking.
     */
    QEMUTimerListGroup tlg;

    int external_disable_cnt;

    /* Number of AioHandlers without .io_poll() */
    int poll_disable_cnt;

    /* Polling mode parameters */
    int64_t poll_ns;        /* current polling time in nanoseconds */
    int64_t poll_max_ns;    /* maximum polling time in nanoseconds */
    int64_t poll_grow;      /* polling time growth factor */
    int64_t poll_shrink;    /* polling time shrink factor */

    /* Are we in polling mode or monitoring file descriptors? */
    bool poll_started;

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
 * Note that this is separate from bdrv_drained_begin/bdrv_drained_end.  A
 * thread still has to call those to avoid being interrupted by the guest.
 *
 * Bottom halves, timers and callbacks can be created or removed without
 * acquiring the AioContext.
 */
void aio_context_acquire(AioContext *ctx);

/* Relinquish ownership of the AioContext. */
void aio_context_release(AioContext *ctx);

/**
 * aio_bh_schedule_oneshot: Allocate a new bottom half structure that will run
 * only once and as soon as possible.
 */
void aio_bh_schedule_oneshot(AioContext *ctx, QEMUBHFunc *cb, void *opaque);

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
 * aio_poll to exit, so that the next call will re-examine pending events.
 * The caller of aio_notify will usually call aio_poll again very soon,
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
void aio_dispatch(AioContext *ctx);

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
                        AioPollFn *io_poll,
                        void *opaque);

/* Set polling begin/end callbacks for a file descriptor that has already been
 * registered with aio_set_fd_handler.  Do nothing if the file descriptor is
 * not registered.
 */
void aio_set_fd_poll(AioContext *ctx, int fd,
                     IOHandler *io_poll_begin,
                     IOHandler *io_poll_end);

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
                            EventNotifierHandler *io_read,
                            AioPollFn *io_poll);

/* Set polling begin/end callbacks for an event notifier that has already been
 * registered with aio_set_event_notifier.  Do nothing if the event notifier is
 * not registered.
 */
void aio_set_event_notifier_poll(AioContext *ctx,
                                 EventNotifier *notifier,
                                 EventNotifierHandler *io_poll_begin,
                                 EventNotifierHandler *io_poll_end);

/* Return a GSource that lets the main loop poll the file descriptors attached
 * to this AioContext.
 */
GSource *aio_get_g_source(AioContext *ctx);

/* Return the ThreadPool bound to this AioContext */
struct ThreadPool *aio_get_thread_pool(AioContext *ctx);

/* Setup the LinuxAioState bound to this AioContext */
struct LinuxAioState *aio_setup_linux_aio(AioContext *ctx, Error **errp);

/* Return the LinuxAioState bound to this AioContext */
struct LinuxAioState *aio_get_linux_aio(AioContext *ctx);

/**
 * aio_timer_new_with_attrs:
 * @ctx: the aio context
 * @type: the clock type
 * @scale: the scale
 * @attributes: 0, or one to multiple OR'ed QEMU_TIMER_ATTR_<id> values
 *              to assign
 * @cb: the callback to call on timer expiry
 * @opaque: the opaque pointer to pass to the callback
 *
 * Allocate a new timer (with attributes) attached to the context @ctx.
 * The function is responsible for memory allocation.
 *
 * The preferred interface is aio_timer_init or aio_timer_init_with_attrs.
 * Use that unless you really need dynamic memory allocation.
 *
 * Returns: a pointer to the new timer
 */
static inline QEMUTimer *aio_timer_new_with_attrs(AioContext *ctx,
                                                  QEMUClockType type,
                                                  int scale, int attributes,
                                                  QEMUTimerCB *cb, void *opaque)
{
    return timer_new_full(&ctx->tlg, type, scale, attributes, cb, opaque);
}

/**
 * aio_timer_new:
 * @ctx: the aio context
 * @type: the clock type
 * @scale: the scale
 * @cb: the callback to call on timer expiry
 * @opaque: the opaque pointer to pass to the callback
 *
 * Allocate a new timer attached to the context @ctx.
 * See aio_timer_new_with_attrs for details.
 *
 * Returns: a pointer to the new timer
 */
static inline QEMUTimer *aio_timer_new(AioContext *ctx, QEMUClockType type,
                                       int scale,
                                       QEMUTimerCB *cb, void *opaque)
{
    return timer_new_full(&ctx->tlg, type, scale, 0, cb, opaque);
}

/**
 * aio_timer_init_with_attrs:
 * @ctx: the aio context
 * @ts: the timer
 * @type: the clock type
 * @scale: the scale
 * @attributes: 0, or one to multiple OR'ed QEMU_TIMER_ATTR_<id> values
 *              to assign
 * @cb: the callback to call on timer expiry
 * @opaque: the opaque pointer to pass to the callback
 *
 * Initialise a new timer (with attributes) attached to the context @ctx.
 * The caller is responsible for memory allocation.
 */
static inline void aio_timer_init_with_attrs(AioContext *ctx,
                                             QEMUTimer *ts, QEMUClockType type,
                                             int scale, int attributes,
                                             QEMUTimerCB *cb, void *opaque)
{
    timer_init_full(ts, &ctx->tlg, type, scale, attributes, cb, opaque);
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
 * See aio_timer_init_with_attrs for details.
 */
static inline void aio_timer_init(AioContext *ctx,
                                  QEMUTimer *ts, QEMUClockType type,
                                  int scale,
                                  QEMUTimerCB *cb, void *opaque)
{
    timer_init_full(ts, &ctx->tlg, type, scale, 0, cb, opaque);
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
    int old;

    old = atomic_fetch_dec(&ctx->external_disable_cnt);
    assert(old > 0);
    if (old == 1) {
        /* Kick event loop so it re-arms file descriptors */
        aio_notify(ctx);
    }
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
 * aio_co_schedule:
 * @ctx: the aio context
 * @co: the coroutine
 *
 * Start a coroutine on a remote AioContext.
 *
 * The coroutine must not be entered by anyone else while aio_co_schedule()
 * is active.  In addition the coroutine must have yielded unless ctx
 * is the context in which the coroutine is running (i.e. the value of
 * qemu_get_current_aio_context() from the coroutine itself).
 */
void aio_co_schedule(AioContext *ctx, struct Coroutine *co);

/**
 * aio_co_wake:
 * @co: the coroutine
 *
 * Restart a coroutine on the AioContext where it was running last, thus
 * preventing coroutines from jumping from one context to another when they
 * go to sleep.
 *
 * aio_co_wake may be executed either in coroutine or non-coroutine
 * context.  The coroutine must not be entered by anyone else while
 * aio_co_wake() is active.
 */
void aio_co_wake(struct Coroutine *co);

/**
 * aio_co_enter:
 * @ctx: the context to run the coroutine
 * @co: the coroutine to run
 *
 * Enter a coroutine in the specified AioContext.
 */
void aio_co_enter(AioContext *ctx, struct Coroutine *co);

/**
 * Return the AioContext whose event loop runs in the current thread.
 *
 * If called from an IOThread this will be the IOThread's AioContext.  If
 * called from another thread it will be the main loop AioContext.
 */
AioContext *qemu_get_current_aio_context(void);

/**
 * in_aio_context_home_thread:
 * @ctx: the aio context
 *
 * Return whether we are running in the thread that normally runs @ctx.  Note
 * that acquiring/releasing ctx does not affect the outcome, each AioContext
 * still only has one home thread that is responsible for running it.
 */
static inline bool in_aio_context_home_thread(AioContext *ctx)
{
    return ctx == qemu_get_current_aio_context();
}

/**
 * aio_context_setup:
 * @ctx: the aio context
 *
 * Initialize the aio context.
 */
void aio_context_setup(AioContext *ctx);

/**
 * aio_context_destroy:
 * @ctx: the aio context
 *
 * Destroy the aio context.
 */
void aio_context_destroy(AioContext *ctx);

/**
 * aio_context_set_poll_params:
 * @ctx: the aio context
 * @max_ns: how long to busy poll for, in nanoseconds
 * @grow: polling time growth factor
 * @shrink: polling time shrink factor
 *
 * Poll mode can be disabled by setting poll_max_ns to 0.
 */
void aio_context_set_poll_params(AioContext *ctx, int64_t max_ns,
                                 int64_t grow, int64_t shrink,
                                 Error **errp);

#endif
