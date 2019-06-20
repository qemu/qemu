/*
 *  Self-announce facility
 *  (c) 2017-2019 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_NET_ANNOUNCE_H
#define QEMU_NET_ANNOUNCE_H

#include "qapi/qapi-types-net.h"
#include "qemu/timer.h"

struct AnnounceTimer {
    QEMUTimer *tm;
    AnnounceParameters params;
    QEMUClockType type;
    int round;
};

/* Returns: update the timer to the next time point */
int64_t qemu_announce_timer_step(AnnounceTimer *timer);

/*
 * Delete the underlying timer and other data
 * If 'free_named' true and the timer is a named timer, then remove
 * it from the list of named timers and free the AnnounceTimer itself.
 */
void qemu_announce_timer_del(AnnounceTimer *timer, bool free_named);

/*
 * Under BQL/main thread
 * Reset the timer to the given parameters/type/notifier.
 */
void qemu_announce_timer_reset(AnnounceTimer *timer,
                               AnnounceParameters *params,
                               QEMUClockType type,
                               QEMUTimerCB *cb,
                               void *opaque);

void qemu_announce_self(AnnounceTimer *timer, AnnounceParameters *params);

#endif
