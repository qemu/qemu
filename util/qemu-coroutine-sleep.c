/*
 * QEMU coroutine sleep
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Stefan Hajnoczi    <stefanha@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/coroutine_int.h"
#include "qemu/timer.h"
#include "qemu/aio.h"

static const char *qemu_co_sleep_ns__scheduled = "qemu_co_sleep_ns";

/*
 * Sentinel stored in QemuCoSleep::to_wake by qemu_co_sleep_wake() when no
 * sleeper has parked yet. The next qemu_co_sleep() consumes it and returns
 * without yielding, so a wake that races the arming of a sleep is never
 * lost.
 */
#define QEMU_CO_SLEEP_PENDING ((Coroutine *)(uintptr_t)1)

void qemu_co_sleep_wake(QemuCoSleep *w)
{
    Coroutine *co;

    co = qatomic_xchg(&w->to_wake, QEMU_CO_SLEEP_PENDING);
    if (co == NULL || co == QEMU_CO_SLEEP_PENDING) {
        /* No sleeper, or a wake is already pending. */
        return;
    }

    /* Write of scheduled protected by barrier write in aio_co_schedule */
    const char *scheduled = qatomic_cmpxchg(&co->scheduled,
                                            qemu_co_sleep_ns__scheduled, NULL);
    assert(scheduled == qemu_co_sleep_ns__scheduled);
    aio_co_wake(co);
}

static void co_sleep_cb(void *opaque)
{
    QemuCoSleep *w = opaque;
    qemu_co_sleep_wake(w);
}

void coroutine_fn qemu_co_sleep(QemuCoSleep *w)
{
    Coroutine *co = qemu_coroutine_self();
    Coroutine *prev;

    const char *scheduled = qatomic_cmpxchg(&co->scheduled, NULL,
                                            qemu_co_sleep_ns__scheduled);
    if (scheduled) {
        fprintf(stderr,
                "%s: Co-routine was already scheduled in '%s'\n",
                __func__, scheduled);
        abort();
    }

    /*
     * Publish ourselves as the sleeper. A wake delivered before we got here,
     * or one racing this publish, leaves QEMU_CO_SLEEP_PENDING in to_wake;
     * the cmpxchg then fails and we consume the wake without yielding.
     */
    prev = qatomic_cmpxchg(&w->to_wake, NULL, co);
    if (prev == QEMU_CO_SLEEP_PENDING) {
        qatomic_set(&w->to_wake, NULL);
        qatomic_set(&co->scheduled, NULL);
        return;
    }
    assert(prev == NULL);

    qemu_coroutine_yield();

    /* The waker left QEMU_CO_SLEEP_PENDING; clear it for the next sleep. */
    qatomic_set(&w->to_wake, NULL);
}

void coroutine_fn qemu_co_sleep_ns_wakeable(QemuCoSleep *w,
                                            QEMUClockType type, int64_t ns)
{
    AioContext *ctx = qemu_get_current_aio_context();
    QEMUTimer ts;

    aio_timer_init(ctx, &ts, type, SCALE_NS, co_sleep_cb, w);
    timer_mod(&ts, qemu_clock_get_ns(type) + ns);

    /*
     * A wake racing with the arming of the sleep -- including the timer
     * we just armed firing in another AioContext before qemu_co_sleep()
     * publishes itself -- is captured by the sticky PENDING state in
     * qemu_co_sleep_wake() and consumed here without yielding.
     */
    qemu_co_sleep(w);
    timer_del(&ts);
}
