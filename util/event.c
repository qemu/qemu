/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu/thread.h"

/*
 * Valid transitions:
 * - FREE -> SET (qemu_event_set)
 * - BUSY -> SET (qemu_event_set)
 * - SET -> FREE (qemu_event_reset)
 * - FREE -> BUSY (qemu_event_wait)
 *
 * With futex, the waking and blocking operations follow
 * BUSY -> SET and FREE -> BUSY, respectively.
 *
 * Without futex, BUSY -> SET and FREE -> BUSY never happen. Instead, the waking
 * operation follows FREE -> SET and the blocking operation will happen in
 * qemu_event_wait() if the event is not SET.
 *
 * SET->BUSY does not happen (it can be observed from the outside but
 * it really is SET->FREE->BUSY).
 *
 * busy->free provably cannot happen; to enforce it, the set->free transition
 * is done with an OR, which becomes a no-op if the event has concurrently
 * transitioned to free or busy.
 */

#define EV_SET         0
#define EV_FREE        1
#define EV_BUSY       -1

void qemu_event_init(QemuEvent *ev, bool init)
{
#ifndef HAVE_FUTEX
    pthread_mutex_init(&ev->lock, NULL);
    pthread_cond_init(&ev->cond, NULL);
#endif

    ev->value = (init ? EV_SET : EV_FREE);
    ev->initialized = true;
}

void qemu_event_destroy(QemuEvent *ev)
{
    assert(ev->initialized);
    ev->initialized = false;
#ifndef HAVE_FUTEX
    pthread_mutex_destroy(&ev->lock);
    pthread_cond_destroy(&ev->cond);
#endif
}

void qemu_event_set(QemuEvent *ev)
{
    assert(ev->initialized);

#ifdef HAVE_FUTEX
    /*
     * Pairs with both qemu_event_reset() and qemu_event_wait().
     *
     * qemu_event_set has release semantics, but because it *loads*
     * ev->value we need a full memory barrier here.
     */
    smp_mb();
    if (qatomic_read(&ev->value) != EV_SET) {
        int old = qatomic_xchg(&ev->value, EV_SET);

        /* Pairs with memory barrier in kernel futex_wait system call.  */
        smp_mb__after_rmw();
        if (old == EV_BUSY) {
            /* There were waiters, wake them up.  */
            qemu_futex_wake_all(ev);
        }
    }
#else
    pthread_mutex_lock(&ev->lock);
    /* Pairs with qemu_event_reset()'s load acquire.  */
    qatomic_store_release(&ev->value, EV_SET);
    pthread_cond_broadcast(&ev->cond);
    pthread_mutex_unlock(&ev->lock);
#endif
}

void qemu_event_reset(QemuEvent *ev)
{
    assert(ev->initialized);

#ifdef HAVE_FUTEX
    /*
     * If there was a concurrent reset (or even reset+wait),
     * do nothing.  Otherwise change EV_SET->EV_FREE.
     */
    qatomic_or(&ev->value, EV_FREE);

    /*
     * Order reset before checking the condition in the caller.
     * Pairs with the first memory barrier in qemu_event_set().
     */
    smp_mb__after_rmw();
#else
    /*
     * If futexes are not available, there are no EV_FREE->EV_BUSY
     * transitions because wakeups are done entirely through the
     * condition variable.  Since qatomic_set() only writes EV_FREE,
     * the load seems useless but in reality, the acquire synchronizes
     * with qemu_event_set()'s store release: if qemu_event_reset()
     * sees EV_SET here, then the caller will certainly see a
     * successful condition and skip qemu_event_wait():
     *
     * done = 1;                 if (done == 0)
     * qemu_event_set() {          qemu_event_reset() {
     *   lock();
     *   ev->value = EV_SET ----->     load ev->value
     *                                 ev->value = old value | EV_FREE
     *   cond_broadcast()
     *   unlock();                 }
     * }                           if (done == 0)
     *                               // qemu_event_wait() not called
     */
    qatomic_set(&ev->value, qatomic_load_acquire(&ev->value) | EV_FREE);
#endif
}

void qemu_event_wait(QemuEvent *ev)
{
    assert(ev->initialized);

#ifdef HAVE_FUTEX
    while (true) {
        /*
         * qemu_event_wait must synchronize with qemu_event_set even if it does
         * not go down the slow path, so this load-acquire is needed that
         * synchronizes with the first memory barrier in qemu_event_set().
         */
        unsigned value = qatomic_load_acquire(&ev->value);
        if (value == EV_SET) {
            break;
        }

        if (value == EV_FREE) {
            /*
             * Leave the event reset and tell qemu_event_set that there are
             * waiters.  No need to retry, because there cannot be a concurrent
             * busy->free transition.  After the CAS, the event will be either
             * set or busy.
             *
             * This cmpxchg doesn't have particular ordering requirements if it
             * succeeds (moving the store earlier can only cause
             * qemu_event_set() to issue _more_ wakeups), the failing case needs
             * acquire semantics like the load above.
             */
            if (qatomic_cmpxchg(&ev->value, EV_FREE, EV_BUSY) == EV_SET) {
                break;
            }
        }

        /*
         * This is the final check for a concurrent set, so it does need
         * a smp_mb() pairing with the second barrier of qemu_event_set().
         * The barrier is inside the FUTEX_WAIT system call.
         */
        qemu_futex_wait(ev, EV_BUSY);
    }
#else
    pthread_mutex_lock(&ev->lock);
    while (qatomic_read(&ev->value) != EV_SET) {
        pthread_cond_wait(&ev->cond, &ev->lock);
    }
    pthread_mutex_unlock(&ev->lock);
#endif
}
