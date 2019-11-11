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

static const char *qemu_co_sleep_ns__scheduled = "qemu_co_sleep_ns";

struct QemuCoSleepState {
    Coroutine *co;
    QEMUTimer *ts;
    QemuCoSleepState **user_state_pointer;
};

void qemu_co_sleep_wake(QemuCoSleepState *sleep_state)
{
    /* Write of schedule protected by barrier write in aio_co_schedule */
    const char *scheduled = atomic_cmpxchg(&sleep_state->co->scheduled,
                                           qemu_co_sleep_ns__scheduled, NULL);

    assert(scheduled == qemu_co_sleep_ns__scheduled);
    if (sleep_state->user_state_pointer) {
        *sleep_state->user_state_pointer = NULL;
    }
    timer_del(sleep_state->ts);
    aio_co_wake(sleep_state->co);
}

static void co_sleep_cb(void *opaque)
{
    qemu_co_sleep_wake(opaque);
}

void coroutine_fn qemu_co_sleep_ns_wakeable(QEMUClockType type, int64_t ns,
                                            QemuCoSleepState **sleep_state)
{
    AioContext *ctx = qemu_get_current_aio_context();
    QemuCoSleepState state = {
        .co = qemu_coroutine_self(),
        .ts = aio_timer_new(ctx, type, SCALE_NS, co_sleep_cb, &state),
        .user_state_pointer = sleep_state,
    };

    const char *scheduled = atomic_cmpxchg(&state.co->scheduled, NULL,
                                           qemu_co_sleep_ns__scheduled);
    if (scheduled) {
        fprintf(stderr,
                "%s: Co-routine was already scheduled in '%s'\n",
                __func__, scheduled);
        abort();
    }

    if (sleep_state) {
        *sleep_state = &state;
    }
    timer_mod(state.ts, qemu_clock_get_ns(type) + ns);
    qemu_coroutine_yield();
    if (sleep_state) {
        /*
         * Note that *sleep_state is cleared during qemu_co_sleep_wake
         * before resuming this coroutine.
         */
        assert(*sleep_state == NULL);
    }
    timer_free(state.ts);
}
