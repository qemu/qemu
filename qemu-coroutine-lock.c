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
#include "block/coroutine.h"
#include "block/coroutine_int.h"
#include "qemu/queue.h"
#include "block/aio.h"
#include "trace.h"

/* Coroutines are awoken from a BH to allow the current coroutine to complete
 * its flow of execution.  The BH may run after the CoQueue has been destroyed,
 * so keep BH data in a separate heap-allocated struct.
 */
typedef struct {
    QEMUBH *bh;
    QTAILQ_HEAD(, Coroutine) entries;
} CoQueueNextData;

static void qemu_co_queue_next_bh(void *opaque)
{
    CoQueueNextData *data = opaque;
    Coroutine *next;

    trace_qemu_co_queue_next_bh();
    while ((next = QTAILQ_FIRST(&data->entries))) {
        QTAILQ_REMOVE(&data->entries, next, co_queue_next);
        qemu_coroutine_enter(next, NULL);
    }

    qemu_bh_delete(data->bh);
    g_slice_free(CoQueueNextData, data);
}

void qemu_co_queue_init(CoQueue *queue)
{
    QTAILQ_INIT(&queue->entries);

    /* This will be exposed to callers once there are multiple AioContexts */
    queue->ctx = qemu_get_aio_context();
}

void coroutine_fn qemu_co_queue_wait(CoQueue *queue)
{
    Coroutine *self = qemu_coroutine_self();
    QTAILQ_INSERT_TAIL(&queue->entries, self, co_queue_next);
    qemu_coroutine_yield();
    assert(qemu_in_coroutine());
}

void coroutine_fn qemu_co_queue_wait_insert_head(CoQueue *queue)
{
    Coroutine *self = qemu_coroutine_self();
    QTAILQ_INSERT_HEAD(&queue->entries, self, co_queue_next);
    qemu_coroutine_yield();
    assert(qemu_in_coroutine());
}

static bool qemu_co_queue_do_restart(CoQueue *queue, bool single)
{
    Coroutine *next;
    CoQueueNextData *data;

    if (QTAILQ_EMPTY(&queue->entries)) {
        return false;
    }

    data = g_slice_new(CoQueueNextData);
    data->bh = aio_bh_new(queue->ctx, qemu_co_queue_next_bh, data);
    QTAILQ_INIT(&data->entries);
    qemu_bh_schedule(data->bh);

    while ((next = QTAILQ_FIRST(&queue->entries)) != NULL) {
        QTAILQ_REMOVE(&queue->entries, next, co_queue_next);
        QTAILQ_INSERT_TAIL(&data->entries, next, co_queue_next);
        trace_qemu_co_queue_next(next);
        if (single) {
            break;
        }
    }
    return true;
}

bool qemu_co_queue_next(CoQueue *queue)
{
    return qemu_co_queue_do_restart(queue, true);
}

void qemu_co_queue_restart_all(CoQueue *queue)
{
    qemu_co_queue_do_restart(queue, false);
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

void qemu_co_rwlock_init(CoRwlock *lock)
{
    memset(lock, 0, sizeof(*lock));
    qemu_co_queue_init(&lock->queue);
}

void qemu_co_rwlock_rdlock(CoRwlock *lock)
{
    while (lock->writer) {
        qemu_co_queue_wait(&lock->queue);
    }
    lock->reader++;
}

void qemu_co_rwlock_unlock(CoRwlock *lock)
{
    assert(qemu_in_coroutine());
    if (lock->writer) {
        lock->writer = false;
        qemu_co_queue_restart_all(&lock->queue);
    } else {
        lock->reader--;
        assert(lock->reader >= 0);
        /* Wakeup only one waiting writer */
        if (!lock->reader) {
            qemu_co_queue_next(&lock->queue);
        }
    }
}

void qemu_co_rwlock_wrlock(CoRwlock *lock)
{
    while (lock->writer || lock->reader) {
        qemu_co_queue_wait(&lock->queue);
    }
    lock->writer = true;
}
