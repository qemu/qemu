/*
 * AioContext wait support
 *
 * Copyright (C) 2018 Red Hat, Inc.
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

#ifndef QEMU_AIO_WAIT_H
#define QEMU_AIO_WAIT_H

#include "block/aio.h"

/**
 * AioWait:
 *
 * An object that facilitates synchronous waiting on a condition. A single
 * global AioWait object (global_aio_wait) is used internally.
 *
 * The main loop can wait on an operation running in an IOThread as follows:
 *
 *   AioContext *ctx = ...;
 *   MyWork work = { .done = false };
 *   schedule_my_work_in_iothread(ctx, &work);
 *   AIO_WAIT_WHILE(ctx, !work.done);
 *
 * The IOThread must call aio_wait_kick() to notify the main loop when
 * work.done changes:
 *
 *   static void do_work(...)
 *   {
 *       ...
 *       work.done = true;
 *       aio_wait_kick();
 *   }
 */
typedef struct {
    /* Number of waiting AIO_WAIT_WHILE() callers. Accessed with atomic ops. */
    unsigned num_waiters;
} AioWait;

extern AioWait global_aio_wait;

/**
 * AIO_WAIT_WHILE:
 * @ctx: the aio context, or NULL if multiple aio contexts (for which the
 *       caller does not hold a lock) are involved in the polling condition.
 * @cond: wait while this conditional expression is true
 *
 * Wait while a condition is true.  Use this to implement synchronous
 * operations that require event loop activity.
 *
 * The caller must be sure that something calls aio_wait_kick() when the value
 * of @cond might have changed.
 *
 * The caller's thread must be the IOThread that owns @ctx or the main loop
 * thread (with @ctx acquired exactly once).  This function cannot be used to
 * wait on conditions between two IOThreads since that could lead to deadlock,
 * go via the main loop instead.
 */
#define AIO_WAIT_WHILE(ctx, cond) ({                               \
    bool waited_ = false;                                          \
    AioWait *wait_ = &global_aio_wait;                             \
    AioContext *ctx_ = (ctx);                                      \
    /* Increment wait_->num_waiters before evaluating cond. */     \
    atomic_inc(&wait_->num_waiters);                               \
    if (ctx_ && in_aio_context_home_thread(ctx_)) {                \
        while ((cond)) {                                           \
            aio_poll(ctx_, true);                                  \
            waited_ = true;                                        \
        }                                                          \
    } else {                                                       \
        assert(qemu_get_current_aio_context() ==                   \
               qemu_get_aio_context());                            \
        while ((cond)) {                                           \
            if (ctx_) {                                            \
                aio_context_release(ctx_);                         \
            }                                                      \
            aio_poll(qemu_get_aio_context(), true);                \
            if (ctx_) {                                            \
                aio_context_acquire(ctx_);                         \
            }                                                      \
            waited_ = true;                                        \
        }                                                          \
    }                                                              \
    atomic_dec(&wait_->num_waiters);                               \
    waited_; })

/**
 * aio_wait_kick:
 * Wake up the main thread if it is waiting on AIO_WAIT_WHILE().  During
 * synchronous operations performed in an IOThread, the main thread lets the
 * IOThread's event loop run, waiting for the operation to complete.  A
 * aio_wait_kick() call will wake up the main thread.
 */
void aio_wait_kick(void);

/**
 * aio_wait_bh_oneshot:
 * @ctx: the aio context
 * @cb: the BH callback function
 * @opaque: user data for the BH callback function
 *
 * Run a BH in @ctx and wait for it to complete.
 *
 * Must be called from the main loop thread with @ctx acquired exactly once.
 * Note that main loop event processing may occur.
 */
void aio_wait_bh_oneshot(AioContext *ctx, QEMUBHFunc *cb, void *opaque);

#endif /* QEMU_AIO_WAIT_H */
