/*
 * QEMU coroutine implementation
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Stefan Hajnoczi    <stefanha@linux.vnet.ibm.com>
 *  Kevin Wolf         <kwolf@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef QEMU_COROUTINE_H
#define QEMU_COROUTINE_H

#include "qemu/queue.h"
#include "qemu/timer.h"

/**
 * Coroutines are a mechanism for stack switching and can be used for
 * cooperative userspace threading.  These functions provide a simple but
 * useful flavor of coroutines that is suitable for writing sequential code,
 * rather than callbacks, for operations that need to give up control while
 * waiting for events to complete.
 *
 * These functions are re-entrant and may be used outside the global mutex.
 */

/**
 * Mark a function that executes in coroutine context
 *
 * Functions that execute in coroutine context cannot be called directly from
 * normal functions.  In the future it would be nice to enable compiler or
 * static checker support for catching such errors.  This annotation might make
 * it possible and in the meantime it serves as documentation.
 *
 * For example:
 *
 *   static void coroutine_fn foo(void) {
 *       ....
 *   }
 */
#define coroutine_fn

typedef struct Coroutine Coroutine;

/**
 * Coroutine entry point
 *
 * When the coroutine is entered for the first time, opaque is passed in as an
 * argument.
 *
 * When this function returns, the coroutine is destroyed automatically and
 * execution continues in the caller who last entered the coroutine.
 */
typedef void coroutine_fn CoroutineEntry(void *opaque);

/**
 * Create a new coroutine
 *
 * Use qemu_coroutine_enter() to actually transfer control to the coroutine.
 * The opaque argument is passed as the argument to the entry point.
 */
Coroutine *qemu_coroutine_create(CoroutineEntry *entry, void *opaque);

/**
 * Transfer control to a coroutine
 */
void qemu_coroutine_enter(Coroutine *coroutine);

/**
 * Transfer control to a coroutine if it's not active (i.e. part of the call
 * stack of the running coroutine). Otherwise, do nothing.
 */
void qemu_coroutine_enter_if_inactive(Coroutine *co);

/**
 * Transfer control to a coroutine and associate it with ctx
 */
void qemu_aio_coroutine_enter(AioContext *ctx, Coroutine *co);

/**
 * Transfer control back to a coroutine's caller
 *
 * This function does not return until the coroutine is re-entered using
 * qemu_coroutine_enter().
 */
void coroutine_fn qemu_coroutine_yield(void);

/**
 * Get the AioContext of the given coroutine
 */
AioContext *coroutine_fn qemu_coroutine_get_aio_context(Coroutine *co);

/**
 * Get the currently executing coroutine
 */
Coroutine *coroutine_fn qemu_coroutine_self(void);

/**
 * Return whether or not currently inside a coroutine
 *
 * This can be used to write functions that work both when in coroutine context
 * and when not in coroutine context.  Note that such functions cannot use the
 * coroutine_fn annotation since they work outside coroutine context.
 */
bool qemu_in_coroutine(void);

/**
 * Return true if the coroutine is currently entered
 *
 * A coroutine is "entered" if it has not yielded from the current
 * qemu_coroutine_enter() call used to run it.  This does not mean that the
 * coroutine is currently executing code since it may have transferred control
 * to another coroutine using qemu_coroutine_enter().
 *
 * When several coroutines enter each other there may be no way to know which
 * ones have already been entered.  In such situations this function can be
 * used to avoid recursively entering coroutines.
 */
bool qemu_coroutine_entered(Coroutine *co);

/**
 * Provides a mutex that can be used to synchronise coroutines
 */
struct CoWaitRecord;
struct CoMutex {
    /* Count of pending lockers; 0 for a free mutex, 1 for an
     * uncontended mutex.
     */
    unsigned locked;

    /* Context that is holding the lock.  Useful to avoid spinning
     * when two coroutines on the same AioContext try to get the lock. :)
     */
    AioContext *ctx;

    /* A queue of waiters.  Elements are added atomically in front of
     * from_push.  to_pop is only populated, and popped from, by whoever
     * is in charge of the next wakeup.  This can be an unlocker or,
     * through the handoff protocol, a locker that is about to go to sleep.
     */
    QSLIST_HEAD(, CoWaitRecord) from_push, to_pop;

    unsigned handoff, sequence;

    Coroutine *holder;
};

/**
 * Initialises a CoMutex. This must be called before any other operation is used
 * on the CoMutex.
 */
void qemu_co_mutex_init(CoMutex *mutex);

/**
 * Locks the mutex. If the lock cannot be taken immediately, control is
 * transferred to the caller of the current coroutine.
 */
void coroutine_fn qemu_co_mutex_lock(CoMutex *mutex);

/**
 * Unlocks the mutex and schedules the next coroutine that was waiting for this
 * lock to be run.
 */
void coroutine_fn qemu_co_mutex_unlock(CoMutex *mutex);

/**
 * Assert that the current coroutine holds @mutex.
 */
static inline coroutine_fn void qemu_co_mutex_assert_locked(CoMutex *mutex)
{
    /*
     * mutex->holder doesn't need any synchronisation if the assertion holds
     * true because the mutex protects it. If it doesn't hold true, we still
     * don't mind if another thread takes or releases mutex behind our back,
     * because the condition will be false no matter whether we read NULL or
     * the pointer for any other coroutine.
     */
    assert(qatomic_read(&mutex->locked) &&
           mutex->holder == qemu_coroutine_self());
}

/**
 * CoQueues are a mechanism to queue coroutines in order to continue executing
 * them later.  They are similar to condition variables, but they need help
 * from an external mutex in order to maintain thread-safety.
 */
typedef struct CoQueue {
    QSIMPLEQ_HEAD(, Coroutine) entries;
} CoQueue;

/**
 * Initialise a CoQueue. This must be called before any other operation is used
 * on the CoQueue.
 */
void qemu_co_queue_init(CoQueue *queue);

/**
 * Adds the current coroutine to the CoQueue and transfers control to the
 * caller of the coroutine.  The mutex is unlocked during the wait and
 * locked again afterwards.
 */
#define qemu_co_queue_wait(queue, lock) \
    qemu_co_queue_wait_impl(queue, QEMU_MAKE_LOCKABLE(lock))
void coroutine_fn qemu_co_queue_wait_impl(CoQueue *queue, QemuLockable *lock);

/**
 * Removes the next coroutine from the CoQueue, and wake it up.
 * Returns true if a coroutine was removed, false if the queue is empty.
 */
bool coroutine_fn qemu_co_queue_next(CoQueue *queue);

/**
 * Empties the CoQueue; all coroutines are woken up.
 */
void coroutine_fn qemu_co_queue_restart_all(CoQueue *queue);

/**
 * Removes the next coroutine from the CoQueue, and wake it up.  Unlike
 * qemu_co_queue_next, this function releases the lock during aio_co_wake
 * because it is meant to be used outside coroutine context; in that case, the
 * coroutine is entered immediately, before qemu_co_enter_next returns.
 *
 * If used in coroutine context, qemu_co_enter_next is equivalent to
 * qemu_co_queue_next.
 */
#define qemu_co_enter_next(queue, lock) \
    qemu_co_enter_next_impl(queue, QEMU_MAKE_LOCKABLE(lock))
bool qemu_co_enter_next_impl(CoQueue *queue, QemuLockable *lock);

/**
 * Checks if the CoQueue is empty.
 */
bool qemu_co_queue_empty(CoQueue *queue);


typedef struct CoRwlock {
    int pending_writer;
    int reader;
    CoMutex mutex;
    CoQueue queue;
} CoRwlock;

/**
 * Initialises a CoRwlock. This must be called before any other operation
 * is used on the CoRwlock
 */
void qemu_co_rwlock_init(CoRwlock *lock);

/**
 * Read locks the CoRwlock. If the lock cannot be taken immediately because
 * of a parallel writer, control is transferred to the caller of the current
 * coroutine.
 */
void qemu_co_rwlock_rdlock(CoRwlock *lock);

/**
 * Write Locks the CoRwlock from a reader.  This is a bit more efficient than
 * @qemu_co_rwlock_unlock followed by a separate @qemu_co_rwlock_wrlock.
 * However, if the lock cannot be upgraded immediately, control is transferred
 * to the caller of the current coroutine.  Also, @qemu_co_rwlock_upgrade
 * only overrides CoRwlock fairness if there are no concurrent readers, so
 * another writer might run while @qemu_co_rwlock_upgrade blocks.
 */
void qemu_co_rwlock_upgrade(CoRwlock *lock);

/**
 * Downgrades a write-side critical section to a reader.  Downgrading with
 * @qemu_co_rwlock_downgrade never blocks, unlike @qemu_co_rwlock_unlock
 * followed by @qemu_co_rwlock_rdlock.  This makes it more efficient, but
 * may also sometimes be necessary for correctness.
 */
void qemu_co_rwlock_downgrade(CoRwlock *lock);

/**
 * Write Locks the mutex. If the lock cannot be taken immediately because
 * of a parallel reader, control is transferred to the caller of the current
 * coroutine.
 */
void qemu_co_rwlock_wrlock(CoRwlock *lock);

/**
 * Unlocks the read/write lock and schedules the next coroutine that was
 * waiting for this lock to be run.
 */
void qemu_co_rwlock_unlock(CoRwlock *lock);

typedef struct QemuCoSleepState QemuCoSleepState;

/**
 * Yield the coroutine for a given duration. During this yield, @sleep_state
 * (if not NULL) is set to an opaque pointer, which may be used for
 * qemu_co_sleep_wake(). Be careful, the pointer is set back to zero when the
 * timer fires. Don't save the obtained value to other variables and don't call
 * qemu_co_sleep_wake from another aio context.
 */
void coroutine_fn qemu_co_sleep_ns_wakeable(QEMUClockType type, int64_t ns,
                                            QemuCoSleepState **sleep_state);
static inline void coroutine_fn qemu_co_sleep_ns(QEMUClockType type, int64_t ns)
{
    qemu_co_sleep_ns_wakeable(type, ns, NULL);
}

/**
 * Wake a coroutine if it is sleeping in qemu_co_sleep_ns. The timer will be
 * deleted. @sleep_state must be the variable whose address was given to
 * qemu_co_sleep_ns() and should be checked to be non-NULL before calling
 * qemu_co_sleep_wake().
 */
void qemu_co_sleep_wake(QemuCoSleepState *sleep_state);

/**
 * Yield until a file descriptor becomes readable
 *
 * Note that this function clobbers the handlers for the file descriptor.
 */
void coroutine_fn yield_until_fd_readable(int fd);

#include "qemu/lockable.h"

#endif /* QEMU_COROUTINE_H */
