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

#include "qemu-common.h"
#include "qemu-queue.h"
#include "event_notifier.h"

typedef struct BlockDriverAIOCB BlockDriverAIOCB;
typedef void BlockDriverCompletionFunc(void *opaque, int ret);

typedef struct AIOCBInfo {
    void (*cancel)(BlockDriverAIOCB *acb);
    size_t aiocb_size;
} AIOCBInfo;

struct BlockDriverAIOCB {
    const AIOCBInfo *aiocb_info;
    BlockDriverState *bs;
    BlockDriverCompletionFunc *cb;
    void *opaque;
};

void *qemu_aio_get(const AIOCBInfo *aiocb_info, BlockDriverState *bs,
                   BlockDriverCompletionFunc *cb, void *opaque);
void qemu_aio_release(void *p);

typedef struct AioHandler AioHandler;
typedef void QEMUBHFunc(void *opaque);
typedef void IOHandler(void *opaque);

typedef struct AioContext {
    GSource source;

    /* The list of registered AIO handlers */
    QLIST_HEAD(, AioHandler) aio_handlers;

    /* This is a simple lock used to protect the aio_handlers list.
     * Specifically, it's used to ensure that no callbacks are removed while
     * we're walking and dispatching callbacks.
     */
    int walking_handlers;

    /* Anchor of the list of Bottom Halves belonging to the context */
    struct QEMUBH *first_bh;

    /* A simple lock used to protect the first_bh list, and ensure that
     * no callbacks are removed while we're walking and dispatching callbacks.
     */
    int walking_bh;

    /* Used for aio_notify.  */
    EventNotifier notifier;
} AioContext;

/* Returns 1 if there are still outstanding AIO requests; 0 otherwise */
typedef int (AioFlushEventNotifierHandler)(EventNotifier *e);

/**
 * aio_context_new: Allocate a new AioContext.
 *
 * AioContext provide a mini event-loop that can be waited on synchronously.
 * They also provide bottom halves, a service to execute a piece of code
 * as soon as possible.
 */
AioContext *aio_context_new(void);

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
 * aio_bh_poll: Poll bottom halves for an AioContext.
 *
 * These are internal functions used by the QEMU main loop.
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
 *
 * @bh: The bottom half to be deleted.
 */
void qemu_bh_delete(QEMUBH *bh);

/* Flush any pending AIO operation. This function will block until all
 * outstanding AIO operations have been completed or cancelled. */
void aio_flush(AioContext *ctx);

/* Return whether there are any pending callbacks from the GSource
 * attached to the AioContext.
 *
 * This is used internally in the implementation of the GSource.
 */
bool aio_pending(AioContext *ctx);

/* Progress in completing AIO work to occur.  This can issue new pending
 * aio as a result of executing I/O completion or bh callbacks.
 *
 * If there is no pending AIO operation or completion (bottom half),
 * return false.  If there are pending bottom halves, return true.
 *
 * If there are no pending bottom halves, but there are pending AIO
 * operations, it may not be possible to make any progress without
 * blocking.  If @blocking is true, this function will wait until one
 * or more AIO events have completed, to ensure something has moved
 * before returning.
 *
 * If @blocking is false, this function will also return false if the
 * function cannot make any progress without blocking.
 */
bool aio_poll(AioContext *ctx, bool blocking);

#ifdef CONFIG_POSIX
/* Returns 1 if there are still outstanding AIO requests; 0 otherwise */
typedef int (AioFlushHandler)(void *opaque);

/* Register a file descriptor and associated callbacks.  Behaves very similarly
 * to qemu_set_fd_handler2.  Unlike qemu_set_fd_handler2, these callbacks will
 * be invoked when using either qemu_aio_wait() or qemu_aio_flush().
 *
 * Code that invokes AIO completion functions should rely on this function
 * instead of qemu_set_fd_handler[2].
 */
void aio_set_fd_handler(AioContext *ctx,
                        int fd,
                        IOHandler *io_read,
                        IOHandler *io_write,
                        AioFlushHandler *io_flush,
                        void *opaque);
#endif

/* Register an event notifier and associated callbacks.  Behaves very similarly
 * to event_notifier_set_handler.  Unlike event_notifier_set_handler, these callbacks
 * will be invoked when using either qemu_aio_wait() or qemu_aio_flush().
 *
 * Code that invokes AIO completion functions should rely on this function
 * instead of event_notifier_set_handler.
 */
void aio_set_event_notifier(AioContext *ctx,
                            EventNotifier *notifier,
                            EventNotifierHandler *io_read,
                            AioFlushEventNotifierHandler *io_flush);

/* Return a GSource that lets the main loop poll the file descriptors attached
 * to this AioContext.
 */
GSource *aio_get_g_source(AioContext *ctx);

/* Functions to operate on the main QEMU AioContext.  */

void qemu_aio_flush(void);
bool qemu_aio_wait(void);
void qemu_aio_set_event_notifier(EventNotifier *notifier,
                                 EventNotifierHandler *io_read,
                                 AioFlushEventNotifierHandler *io_flush);

#ifdef CONFIG_POSIX
void qemu_aio_set_fd_handler(int fd,
                             IOHandler *io_read,
                             IOHandler *io_write,
                             AioFlushHandler *io_flush,
                             void *opaque);
#endif

#endif
