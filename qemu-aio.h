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
#include "event_notifier.h"

typedef struct BlockDriverAIOCB BlockDriverAIOCB;
typedef void BlockDriverCompletionFunc(void *opaque, int ret);

typedef struct AIOPool {
    void (*cancel)(BlockDriverAIOCB *acb);
    int aiocb_size;
    BlockDriverAIOCB *free_aiocb;
} AIOPool;

struct BlockDriverAIOCB {
    AIOPool *pool;
    BlockDriverState *bs;
    BlockDriverCompletionFunc *cb;
    void *opaque;
    BlockDriverAIOCB *next;
};

void *qemu_aio_get(AIOPool *pool, BlockDriverState *bs,
                   BlockDriverCompletionFunc *cb, void *opaque);
void qemu_aio_release(void *p);

typedef struct AioHandler AioHandler;
typedef void QEMUBHFunc(void *opaque);
typedef void IOHandler(void *opaque);

typedef struct AioContext {
    /* Anchor of the list of Bottom Halves belonging to the context */
    struct QEMUBH *first_bh;

    /* A simple lock used to protect the first_bh list, and ensure that
     * no callbacks are removed while we're walking and dispatching callbacks.
     */
    int walking_bh;
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
 * aio_bh_new: Allocate a new bottom half structure.
 *
 * Bottom halves are lightweight callbacks whose invocation is guaranteed
 * to be wait-free, thread-safe and signal-safe.  The #QEMUBH structure
 * is opaque and must be allocated prior to its use.
 */
QEMUBH *aio_bh_new(AioContext *ctx, QEMUBHFunc *cb, void *opaque);

/**
 * aio_bh_poll: Poll bottom halves for an AioContext.
 *
 * These are internal functions used by the QEMU main loop.
 */
int aio_bh_poll(AioContext *ctx);
void aio_bh_update_timeout(AioContext *ctx, uint32_t *timeout);

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
void qemu_aio_flush(void);

/* Wait for a single AIO completion to occur.  This function will wait
 * until a single AIO event has completed and it will ensure something
 * has moved before returning. This can issue new pending aio as
 * result of executing I/O completion or bh callbacks.
 *
 * Return whether there is still any pending AIO operation.  */
bool qemu_aio_wait(void);

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
void qemu_aio_set_fd_handler(int fd,
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
void qemu_aio_set_event_notifier(EventNotifier *notifier,
                                 EventNotifierHandler *io_read,
                                 AioFlushEventNotifierHandler *io_flush);

#endif
