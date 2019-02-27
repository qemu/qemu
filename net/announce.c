/*
 *  Self-announce
 *  (c) 2017-2019 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "net/announce.h"
#include "qapi/clone-visitor.h"
#include "qapi/qapi-visit-net.h"

int64_t qemu_announce_timer_step(AnnounceTimer *timer)
{
    int64_t step;

    step =  timer->params.initial +
            (timer->params.rounds - timer->round - 1) *
            timer->params.step;

    if (step < 0 || step > timer->params.max) {
        step = timer->params.max;
    }
    timer_mod(timer->tm, qemu_clock_get_ms(timer->type) + step);

    return step;
}

void qemu_announce_timer_del(AnnounceTimer *timer)
{
    if (timer->tm) {
        timer_del(timer->tm);
        timer_free(timer->tm);
        timer->tm = NULL;
    }
}

/*
 * Under BQL/main thread
 * Reset the timer to the given parameters/type/notifier.
 */
void qemu_announce_timer_reset(AnnounceTimer *timer,
                               AnnounceParameters *params,
                               QEMUClockType type,
                               QEMUTimerCB *cb,
                               void *opaque)
{
    /*
     * We're under the BQL, so the current timer can't
     * be firing, so we should be able to delete it.
     */
    qemu_announce_timer_del(timer);

    QAPI_CLONE_MEMBERS(AnnounceParameters, &timer->params, params);
    timer->round = params->rounds;
    timer->type = type;
    timer->tm = timer_new_ms(type, cb, opaque);
}
