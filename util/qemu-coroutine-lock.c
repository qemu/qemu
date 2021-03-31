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
 *
 * The lock-free mutex implementation is based on OSv
 * (core/lfmutex.cc, include/lockfree/mutex.hh).
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 */

#include "qemu/osdep.h"
#include "qemu/coroutine.h"
#include "qemu/coroutine_int.h"
#include "qemu/processor.h"
#include "qemu/queue.h"
#include "block/aio.h"
#include "trace.h"

void qemu_co_queue_init(CoQueue *queue)
{
    QSIMPLEQ_INIT(&queue->entries);
}

void coroutine_fn qemu_co_queue_wait_impl(CoQueue *queue, QemuLockable *lock)
{
    Coroutine *self = qemu_coroutine_self();
    QSIMPLEQ_INSERT_TAIL(&queue->entries, self, co_queue_next);

    if (lock) {
        qemu_lockable_unlock(lock);
    }

    /* There is no race condition here.  Other threads will call
     * aio_co_schedule on our AioContext, which can reenter this
     * coroutine but only after this yield and after the main loop
     * has gone through the next iteration.
     */
    qemu_coroutine_yield();
    assert(qemu_in_coroutine());

    /* TODO: OSv implements wait morphing here, where the wakeup
     * primitive automatically places the woken coroutine on the
     * mutex's queue.  This avoids the thundering herd effect.
     * This could be implemented for CoMutexes, but not really for
     * other cases of QemuLockable.
     */
    if (lock) {
        qemu_lockable_lock(lock);
    }
}

static bool qemu_co_queue_do_restart(CoQueue *queue, bool single)
{
    Coroutine *next;

    if (QSIMPLEQ_EMPTY(&queue->entries)) {
        return false;
    }

    while ((next = QSIMPLEQ_FIRST(&queue->entries)) != NULL) {
        QSIMPLEQ_REMOVE_HEAD(&queue->entries, co_queue_next);
        aio_co_wake(next);
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

bool qemu_co_enter_next_impl(CoQueue *queue, QemuLockable *lock)
{
    Coroutine *next;

    next = QSIMPLEQ_FIRST(&queue->entries);
    if (!next) {
        return false;
    }

    QSIMPLEQ_REMOVE_HEAD(&queue->entries, co_queue_next);
    if (lock) {
        qemu_lockable_unlock(lock);
    }
    aio_co_wake(next);
    if (lock) {
        qemu_lockable_lock(lock);
    }
    return true;
}

bool qemu_co_queue_empty(CoQueue *queue)
{
    return QSIMPLEQ_FIRST(&queue->entries) == NULL;
}

/* The wait records are handled with a multiple-producer, single-consumer
 * lock-free queue.  There cannot be two concurrent pop_waiter() calls
 * because pop_waiter() can only be called while mutex->handoff is zero.
 * This can happen in three cases:
 * - in qemu_co_mutex_unlock, before the hand-off protocol has started.
 *   In this case, qemu_co_mutex_lock will see mutex->handoff == 0 and
 *   not take part in the handoff.
 * - in qemu_co_mutex_lock, if it steals the hand-off responsibility from
 *   qemu_co_mutex_unlock.  In this case, qemu_co_mutex_unlock will fail
 *   the cmpxchg (it will see either 0 or the next sequence value) and
 *   exit.  The next hand-off cannot begin until qemu_co_mutex_lock has
 *   woken up someone.
 * - in qemu_co_mutex_unlock, if it takes the hand-off token itself.
 *   In this case another iteration starts with mutex->handoff == 0;
 *   a concurrent qemu_co_mutex_lock will fail the cmpxchg, and
 *   qemu_co_mutex_unlock will go back to case (1).
 *
 * The following functions manage this queue.
 */
typedef struct CoWaitRecord {
    Coroutine *co;
    QSLIST_ENTRY(CoWaitRecord) next;
} CoWaitRecord;

static void push_waiter(CoMutex *mutex, CoWaitRecord *w)
{
    w->co = qemu_coroutine_self();
    QSLIST_INSERT_HEAD_ATOMIC(&mutex->from_push, w, next);
}

static void move_waiters(CoMutex *mutex)
{
    QSLIST_HEAD(, CoWaitRecord) reversed;
    QSLIST_MOVE_ATOMIC(&reversed, &mutex->from_push);
    while (!QSLIST_EMPTY(&reversed)) {
        CoWaitRecord *w = QSLIST_FIRST(&reversed);
        QSLIST_REMOVE_HEAD(&reversed, next);
        QSLIST_INSERT_HEAD(&mutex->to_pop, w, next);
    }
}

static CoWaitRecord *pop_waiter(CoMutex *mutex)
{
    CoWaitRecord *w;

    if (QSLIST_EMPTY(&mutex->to_pop)) {
        move_waiters(mutex);
        if (QSLIST_EMPTY(&mutex->to_pop)) {
            return NULL;
        }
    }
    w = QSLIST_FIRST(&mutex->to_pop);
    QSLIST_REMOVE_HEAD(&mutex->to_pop, next);
    return w;
}

static bool has_waiters(CoMutex *mutex)
{
    return QSLIST_EMPTY(&mutex->to_pop) || QSLIST_EMPTY(&mutex->from_push);
}

void qemu_co_mutex_init(CoMutex *mutex)
{
    memset(mutex, 0, sizeof(*mutex));
}

static void coroutine_fn qemu_co_mutex_wake(CoMutex *mutex, Coroutine *co)
{
    /* Read co before co->ctx; pairs with smp_wmb() in
     * qemu_coroutine_enter().
     */
    smp_read_barrier_depends();
    mutex->ctx = co->ctx;
    aio_co_wake(co);
}

static void coroutine_fn qemu_co_mutex_lock_slowpath(AioContext *ctx,
                                                     CoMutex *mutex)
{
    Coroutine *self = qemu_coroutine_self();
    CoWaitRecord w;
    unsigned old_handoff;

    trace_qemu_co_mutex_lock_entry(mutex, self);
    push_waiter(mutex, &w);

    /* This is the "Responsibility Hand-Off" protocol; a lock() picks from
     * a concurrent unlock() the responsibility of waking somebody up.
     */
    old_handoff = qatomic_mb_read(&mutex->handoff);
    if (old_handoff &&
        has_waiters(mutex) &&
        qatomic_cmpxchg(&mutex->handoff, old_handoff, 0) == old_handoff) {
        /* There can be no concurrent pops, because there can be only
         * one active handoff at a time.
         */
        CoWaitRecord *to_wake = pop_waiter(mutex);
        Coroutine *co = to_wake->co;
        if (co == self) {
            /* We got the lock ourselves!  */
            assert(to_wake == &w);
            mutex->ctx = ctx;
            return;
        }

        qemu_co_mutex_wake(mutex, co);
    }

    qemu_coroutine_yield();
    trace_qemu_co_mutex_lock_return(mutex, self);
}

void coroutine_fn qemu_co_mutex_lock(CoMutex *mutex)
{
    AioContext *ctx = qemu_get_current_aio_context();
    Coroutine *self = qemu_coroutine_self();
    int waiters, i;

    /* Running a very small critical section on pthread_mutex_t and CoMutex
     * shows that pthread_mutex_t is much faster because it doesn't actually
     * go to sleep.  What happens is that the critical section is shorter
     * than the latency of entering the kernel and thus FUTEX_WAIT always
     * fails.  With CoMutex there is no such latency but you still want to
     * avoid wait and wakeup.  So introduce it artificially.
     */
    i = 0;
retry_fast_path:
    waiters = qatomic_cmpxchg(&mutex->locked, 0, 1);
    if (waiters != 0) {
        while (waiters == 1 && ++i < 1000) {
            if (qatomic_read(&mutex->ctx) == ctx) {
                break;
            }
            if (qatomic_read(&mutex->locked) == 0) {
                goto retry_fast_path;
            }
            cpu_relax();
        }
        waiters = qatomic_fetch_inc(&mutex->locked);
    }

    if (waiters == 0) {
        /* Uncontended.  */
        trace_qemu_co_mutex_lock_uncontended(mutex, self);
        mutex->ctx = ctx;
    } else {
        qemu_co_mutex_lock_slowpath(ctx, mutex);
    }
    mutex->holder = self;
    self->locks_held++;
}

void coroutine_fn qemu_co_mutex_unlock(CoMutex *mutex)
{
    Coroutine *self = qemu_coroutine_self();

    trace_qemu_co_mutex_unlock_entry(mutex, self);

    assert(mutex->locked);
    assert(mutex->holder == self);
    assert(qemu_in_coroutine());

    mutex->ctx = NULL;
    mutex->holder = NULL;
    self->locks_held--;
    if (qatomic_fetch_dec(&mutex->locked) == 1) {
        /* No waiting qemu_co_mutex_lock().  Pfew, that was easy!  */
        return;
    }

    for (;;) {
        CoWaitRecord *to_wake = pop_waiter(mutex);
        unsigned our_handoff;

        if (to_wake) {
            qemu_co_mutex_wake(mutex, to_wake->co);
            break;
        }

        /* Some concurrent lock() is in progress (we know this because
         * mutex->locked was >1) but it hasn't yet put itself on the wait
         * queue.  Pick a sequence number for the handoff protocol (not 0).
         */
        if (++mutex->sequence == 0) {
            mutex->sequence = 1;
        }

        our_handoff = mutex->sequence;
        qatomic_mb_set(&mutex->handoff, our_handoff);
        if (!has_waiters(mutex)) {
            /* The concurrent lock has not added itself yet, so it
             * will be able to pick our handoff.
             */
            break;
        }

        /* Try to do the handoff protocol ourselves; if somebody else has
         * already taken it, however, we're done and they're responsible.
         */
        if (qatomic_cmpxchg(&mutex->handoff, our_handoff, 0) != our_handoff) {
            break;
        }
    }

    trace_qemu_co_mutex_unlock_return(mutex, self);
}

struct CoRwTicket {
    bool read;
    Coroutine *co;
    QSIMPLEQ_ENTRY(CoRwTicket) next;
};

void qemu_co_rwlock_init(CoRwlock *lock)
{
    qemu_co_mutex_init(&lock->mutex);
    lock->owners = 0;
    QSIMPLEQ_INIT(&lock->tickets);
}

/* Releases the internal CoMutex.  */
static void qemu_co_rwlock_maybe_wake_one(CoRwlock *lock)
{
    CoRwTicket *tkt = QSIMPLEQ_FIRST(&lock->tickets);
    Coroutine *co = NULL;

    /*
     * Setting lock->owners here prevents rdlock and wrlock from
     * sneaking in between unlock and wake.
     */

    if (tkt) {
        if (tkt->read) {
            if (lock->owners >= 0) {
                lock->owners++;
                co = tkt->co;
            }
        } else {
            if (lock->owners == 0) {
                lock->owners = -1;
                co = tkt->co;
            }
        }
    }

    if (co) {
        QSIMPLEQ_REMOVE_HEAD(&lock->tickets, next);
        qemu_co_mutex_unlock(&lock->mutex);
        aio_co_wake(co);
    } else {
        qemu_co_mutex_unlock(&lock->mutex);
    }
}

void qemu_co_rwlock_rdlock(CoRwlock *lock)
{
    Coroutine *self = qemu_coroutine_self();

    qemu_co_mutex_lock(&lock->mutex);
    /* For fairness, wait if a writer is in line.  */
    if (lock->owners == 0 || (lock->owners > 0 && QSIMPLEQ_EMPTY(&lock->tickets))) {
        lock->owners++;
        qemu_co_mutex_unlock(&lock->mutex);
    } else {
        CoRwTicket my_ticket = { true, self };

        QSIMPLEQ_INSERT_TAIL(&lock->tickets, &my_ticket, next);
        qemu_co_mutex_unlock(&lock->mutex);
        qemu_coroutine_yield();
        assert(lock->owners >= 1);

        /* Possibly wake another reader, which will wake the next in line.  */
        qemu_co_mutex_lock(&lock->mutex);
        qemu_co_rwlock_maybe_wake_one(lock);
    }

    self->locks_held++;
}

void qemu_co_rwlock_unlock(CoRwlock *lock)
{
    Coroutine *self = qemu_coroutine_self();

    assert(qemu_in_coroutine());
    self->locks_held--;

    qemu_co_mutex_lock(&lock->mutex);
    if (lock->owners > 0) {
        lock->owners--;
    } else {
        assert(lock->owners == -1);
        lock->owners = 0;
    }

    qemu_co_rwlock_maybe_wake_one(lock);
}

void qemu_co_rwlock_downgrade(CoRwlock *lock)
{
    qemu_co_mutex_lock(&lock->mutex);
    assert(lock->owners == -1);
    lock->owners = 1;

    /* Possibly wake another reader, which will wake the next in line.  */
    qemu_co_rwlock_maybe_wake_one(lock);
}

void qemu_co_rwlock_wrlock(CoRwlock *lock)
{
    Coroutine *self = qemu_coroutine_self();

    qemu_co_mutex_lock(&lock->mutex);
    if (lock->owners == 0) {
        lock->owners = -1;
        qemu_co_mutex_unlock(&lock->mutex);
    } else {
        CoRwTicket my_ticket = { false, qemu_coroutine_self() };

        QSIMPLEQ_INSERT_TAIL(&lock->tickets, &my_ticket, next);
        qemu_co_mutex_unlock(&lock->mutex);
        qemu_coroutine_yield();
        assert(lock->owners == -1);
    }

    self->locks_held++;
}

void qemu_co_rwlock_upgrade(CoRwlock *lock)
{
    qemu_co_mutex_lock(&lock->mutex);
    assert(lock->owners > 0);
    /* For fairness, wait if a writer is in line.  */
    if (lock->owners == 1 && QSIMPLEQ_EMPTY(&lock->tickets)) {
        lock->owners = -1;
        qemu_co_mutex_unlock(&lock->mutex);
    } else {
        CoRwTicket my_ticket = { false, qemu_coroutine_self() };

        lock->owners--;
        QSIMPLEQ_INSERT_TAIL(&lock->tickets, &my_ticket, next);
        qemu_co_rwlock_maybe_wake_one(lock);
        qemu_coroutine_yield();
        assert(lock->owners == -1);
    }
}
