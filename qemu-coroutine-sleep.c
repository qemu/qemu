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

#include "qemu-coroutine.h"
#include "qemu-timer.h"

typedef struct CoSleepCB {
    QEMUTimer *ts;
    Coroutine *co;
} CoSleepCB;

static void co_sleep_cb(void *opaque)
{
    CoSleepCB *sleep_cb = opaque;

    qemu_free_timer(sleep_cb->ts);
    qemu_coroutine_enter(sleep_cb->co, NULL);
}

void coroutine_fn co_sleep_ns(QEMUClock *clock, int64_t ns)
{
    CoSleepCB sleep_cb = {
        .co = qemu_coroutine_self(),
    };
    sleep_cb.ts = qemu_new_timer(clock, SCALE_NS, co_sleep_cb, &sleep_cb);
    qemu_mod_timer(sleep_cb.ts, qemu_get_clock_ns(clock) + ns);
    qemu_coroutine_yield();
}
