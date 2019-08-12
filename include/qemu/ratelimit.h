/*
 * Ratelimiting calculations
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Stefan Hajnoczi   <stefanha@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef QEMU_RATELIMIT_H
#define QEMU_RATELIMIT_H

#include "qemu/timer.h"

typedef struct {
    int64_t slice_start_time;
    int64_t slice_end_time;
    uint64_t slice_quota;
    uint64_t slice_ns;
    uint64_t dispatched;
} RateLimit;

/** Calculate and return delay for next request in ns
 *
 * Record that we sent @n data units (where @n matches the scale chosen
 * during ratelimit_set_speed). If we may send more data units
 * in the current time slice, return 0 (i.e. no delay). Otherwise
 * return the amount of time (in ns) until the start of the next time
 * slice that will permit sending the next chunk of data.
 *
 * Recording sent data units even after exceeding the quota is
 * permitted; the time slice will be extended accordingly.
 */
static inline int64_t ratelimit_calculate_delay(RateLimit *limit, uint64_t n)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    double delay_slices;

    assert(limit->slice_quota && limit->slice_ns);

    if (limit->slice_end_time < now) {
        /* Previous, possibly extended, time slice finished; reset the
         * accounting. */
        limit->slice_start_time = now;
        limit->slice_end_time = now + limit->slice_ns;
        limit->dispatched = 0;
    }

    limit->dispatched += n;
    if (limit->dispatched < limit->slice_quota) {
        /* We may send further data within the current time slice, no
         * need to delay the next request. */
        return 0;
    }

    /* Quota exceeded. Wait based on the excess amount and then start a new
     * slice. */
    delay_slices = (double)limit->dispatched / limit->slice_quota;
    limit->slice_end_time = limit->slice_start_time +
        (uint64_t)(delay_slices * limit->slice_ns);
    return limit->slice_end_time - now;
}

static inline void ratelimit_set_speed(RateLimit *limit, uint64_t speed,
                                       uint64_t slice_ns)
{
    limit->slice_ns = slice_ns;
    limit->slice_quota = MAX(((double)speed * slice_ns) / 1000000000ULL, 1);
}

#endif
