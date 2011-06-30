/*
 * coroutine queues and locks
 *
 * Copyright (c) 2011 Kevin Wolf <kwolf@redhat.com>
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
#include "qemu-coroutine.h"
#include "qemu-coroutine-int.h"
#include "qemu-queue.h"
#include "trace.h"

static QTAILQ_HEAD(, Coroutine) unlock_bh_queue =
    QTAILQ_HEAD_INITIALIZER(unlock_bh_queue);

struct unlock_bh {
    QEMUBH *bh;
};

static void qemu_co_queue_next_bh(void *opaque)
{
    struct unlock_bh *unlock_bh = opaque;
    Coroutine *next;

    trace_qemu_co_queue_next_bh();
    while ((next = QTAILQ_FIRST(&unlock_bh_queue))) {
        QTAILQ_REMOVE(&unlock_bh_queue, next, co_queue_next);
        qemu_coroutine_enter(next, NULL);
    }

    qemu_bh_delete(unlock_bh->bh);
    qemu_free(unlock_bh);
}

void qemu_co_queue_init(CoQueue *queue)
{
    QTAILQ_INIT(&queue->entries);
}

void coroutine_fn qemu_co_queue_wait(CoQueue *queue)
{
    Coroutine *self = qemu_coroutine_self();
    QTAILQ_INSERT_TAIL(&queue->entries, self, co_queue_next);
    qemu_coroutine_yield();
    assert(qemu_in_coroutine());
}

bool qemu_co_queue_next(CoQueue *queue)
{
    struct unlock_bh *unlock_bh;
    Coroutine *next;

    next = QTAILQ_FIRST(&queue->entries);
    if (next) {
        QTAILQ_REMOVE(&queue->entries, next, co_queue_next);
        QTAILQ_INSERT_TAIL(&unlock_bh_queue, next, co_queue_next);
        trace_qemu_co_queue_next(next);

        unlock_bh = qemu_malloc(sizeof(*unlock_bh));
        unlock_bh->bh = qemu_bh_new(qemu_co_queue_next_bh, unlock_bh);
        qemu_bh_schedule(unlock_bh->bh);
    }

    return (next != NULL);
}

bool qemu_co_queue_empty(CoQueue *queue)
{
    return (QTAILQ_FIRST(&queue->entries) == NULL);
}

void qemu_co_mutex_init(CoMutex *mutex)
{
    memset(mutex, 0, sizeof(*mutex));
    qemu_co_queue_init(&mutex->queue);
}

void coroutine_fn qemu_co_mutex_lock(CoMutex *mutex)
{
    Coroutine *self = qemu_coroutine_self();

    trace_qemu_co_mutex_lock_entry(mutex, self);

    while (mutex->locked) {
        qemu_co_queue_wait(&mutex->queue);
    }

    mutex->locked = true;

    trace_qemu_co_mutex_lock_return(mutex, self);
}

void coroutine_fn qemu_co_mutex_unlock(CoMutex *mutex)
{
    Coroutine *self = qemu_coroutine_self();

    trace_qemu_co_mutex_unlock_entry(mutex, self);

    assert(mutex->locked == true);
    assert(qemu_in_coroutine());

    mutex->locked = false;
    qemu_co_queue_next(&mutex->queue);

    trace_qemu_co_mutex_unlock_return(mutex, self);
}
