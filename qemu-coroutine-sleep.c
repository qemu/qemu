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

#include "block/coroutine.h"
#include "qemu/timer.h"
#include "block/aio.h"

typedef struct CoSleepCB {
    QEMUTimer *ts;
    Coroutine *co;
} CoSleepCB;

static void co_sleep_cb(void *opaque)
{
    CoSleepCB *sleep_cb = opaque;

    qemu_coroutine_enter(sleep_cb->co, NULL);
}

void coroutine_fn co_sleep_ns(QEMUClockType type, int64_t ns)
{
    CoSleepCB sleep_cb = {
        .co = qemu_coroutine_self(),
    };
    sleep_cb.ts = timer_new(type, SCALE_NS, co_sleep_cb, &sleep_cb);
    timer_mod(sleep_cb.ts, qemu_clock_get_ns(type) + ns);
    qemu_coroutine_yield();
    timer_del(sleep_cb.ts);
    timer_free(sleep_cb.ts);
}

void coroutine_fn co_aio_sleep_ns(AioContext *ctx, QEMUClockType type,
                                  int64_t ns)
{
    CoSleepCB sleep_cb = {
        .co = qemu_coroutine_self(),
    };
    sleep_cb.ts = aio_timer_new(ctx, type, SCALE_NS, co_sleep_cb, &sleep_cb);
    timer_mod(sleep_cb.ts, qemu_clock_get_ns(type) + ns);
    qemu_coroutine_yield();
    timer_del(sleep_cb.ts);
    timer_free(sleep_cb.ts);
}
