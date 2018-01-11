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
#include "qemu/coroutine.h"
#include "qemu/coroutine_int.h"
#include "qemu/timer.h"
#include "block/aio.h"

typedef struct CoSleepCB {
    QEMUTimer *ts;
    Coroutine *co;
} CoSleepCB;

static void co_sleep_cb(void *opaque)
{
    CoSleepCB *sleep_cb = opaque;

    /* Write of schedule protected by barrier write in aio_co_schedule */
    atomic_set(&sleep_cb->co->scheduled, NULL);
    aio_co_wake(sleep_cb->co);
}

void coroutine_fn qemu_co_sleep_ns(QEMUClockType type, int64_t ns)
{
    AioContext *ctx = qemu_get_current_aio_context();
    CoSleepCB sleep_cb = {
        .co = qemu_coroutine_self(),
    };

    const char *scheduled = atomic_cmpxchg(&sleep_cb.co->scheduled, NULL,
                                           __func__);
    if (scheduled) {
        fprintf(stderr,
                "%s: Co-routine was already scheduled in '%s'\n",
                __func__, scheduled);
        abort();
    }
    sleep_cb.ts = aio_timer_new(ctx, type, SCALE_NS, co_sleep_cb, &sleep_cb);
    timer_mod(sleep_cb.ts, qemu_clock_get_ns(type) + ns);
    qemu_coroutine_yield();
    timer_del(sleep_cb.ts);
    timer_free(sleep_cb.ts);
}
