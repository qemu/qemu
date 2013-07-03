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
#define QEMU_RATELIMIT_H 1

typedef struct {
    int64_t next_slice_time;
    uint64_t slice_quota;
    uint64_t slice_ns;
    uint64_t dispatched;
} RateLimit;

static inline int64_t ratelimit_calculate_delay(RateLimit *limit, uint64_t n)
{
    int64_t now = qemu_get_clock_ns(rt_clock);

    if (limit->next_slice_time < now) {
        limit->next_slice_time = now + limit->slice_ns;
        limit->dispatched = 0;
    }
    if (limit->dispatched == 0 || limit->dispatched + n <= limit->slice_quota) {
        limit->dispatched += n;
        return 0;
    } else {
        limit->dispatched = n;
        return limit->next_slice_time - now;
    }
}

static inline void ratelimit_set_speed(RateLimit *limit, uint64_t speed,
                                       uint64_t slice_ns)
{
    limit->slice_ns = slice_ns;
    limit->slice_quota = ((double)speed * slice_ns)/1000000000ULL;
}

#endif
