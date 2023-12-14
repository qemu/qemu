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
#include "block/aio.h"

static const char *qemu_co_sleep_ns__scheduled = "qemu_co_sleep_ns";

void qemu_co_sleep_wake(QemuCoSleep *w)
{
    Coroutine *co;

    co = w->to_wake;
    w->to_wake = NULL;
    if (co) {
        /* Write of schedule protected by barrier write in aio_co_schedule */
        const char *scheduled = qatomic_cmpxchg(&co->scheduled,
                                                qemu_co_sleep_ns__scheduled, NULL);

        assert(scheduled == qemu_co_sleep_ns__scheduled);
        aio_co_wake(co);
    }
}

static void co_sleep_cb(void *opaque)
{
    QemuCoSleep *w = opaque;
    qemu_co_sleep_wake(w);
}

void coroutine_fn qemu_co_sleep(QemuCoSleep *w)
{
    Coroutine *co = qemu_coroutine_self();

    const char *scheduled = qatomic_cmpxchg(&co->scheduled, NULL,
                                            qemu_co_sleep_ns__scheduled);
    if (scheduled) {
        fprintf(stderr,
                "%s: Co-routine was already scheduled in '%s'\n",
                __func__, scheduled);
        abort();
    }

    w->to_wake = co;
    qemu_coroutine_yield();

    /* w->to_wake is cleared before resuming this coroutine.  */
    assert(w->to_wake == NULL);
}

void coroutine_fn qemu_co_sleep_ns_wakeable(QemuCoSleep *w,
                                            QEMUClockType type, int64_t ns)
{
    AioContext *ctx = qemu_get_current_aio_context();
    QEMUTimer ts;

    aio_timer_init(ctx, &ts, type, SCALE_NS, co_sleep_cb, w);
    timer_mod(&ts, qemu_clock_get_ns(type) + ns);

    /*
     * The timer will fire in the current AiOContext, so the callback
     * must happen after qemu_co_sleep yields and there is no race
     * between timer_mod and qemu_co_sleep.
     */
    qemu_co_sleep(w);
    timer_del(&ts);
}
