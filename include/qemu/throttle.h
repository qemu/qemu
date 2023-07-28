/*
 * QEMU throttling infrastructure
 *
 * Copyright (C) Nodalink, EURL. 2013-2014
 * Copyright (C) Igalia, S.L. 2015-2016
 *
 * Authors:
 *   Benoît Canet <benoit.canet@nodalink.com>
 *   Alberto Garcia <berto@igalia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef THROTTLE_H
#define THROTTLE_H

#include "qapi/qapi-types-block-core.h"
#include "qemu/timer.h"

#define THROTTLE_VALUE_MAX 1000000000000000LL

typedef enum {
    THROTTLE_BPS_TOTAL,
    THROTTLE_BPS_READ,
    THROTTLE_BPS_WRITE,
    THROTTLE_OPS_TOTAL,
    THROTTLE_OPS_READ,
    THROTTLE_OPS_WRITE,
    BUCKETS_COUNT,
} BucketType;

/*
 * This module implements I/O limits using the leaky bucket
 * algorithm. The code is independent of the I/O units, but it is
 * currently used for bytes per second and operations per second.
 *
 * Three parameters can be set by the user:
 *
 * - avg: the desired I/O limits in units per second.
 * - max: the limit during bursts, also in units per second.
 * - burst_length: the maximum length of the burst period, in seconds.
 *
 * Here's how it works:
 *
 * - The bucket level (number of performed I/O units) is kept in
 *   bkt.level and leaks at a rate of bkt.avg units per second.
 *
 * - The size of the bucket is bkt.max * bkt.burst_length. Once the
 *   bucket is full no more I/O is performed until the bucket leaks
 *   again. This is what makes the I/O rate bkt.avg.
 *
 * - The bkt.avg rate does not apply until the bucket is full,
 *   allowing the user to do bursts until then. The I/O limit during
 *   bursts is bkt.max. To enforce this limit we keep an additional
 *   bucket in bkt.burst_level that leaks at a rate of bkt.max units
 *   per second.
 *
 * - Because of all of the above, the user can perform I/O at a
 *   maximum of bkt.max units per second for at most bkt.burst_length
 *   seconds in a row. After that the bucket will be full and the I/O
 *   rate will go down to bkt.avg.
 *
 * - Since the bucket always leaks at a rate of bkt.avg, this also
 *   determines how much the user needs to wait before being able to
 *   do bursts again.
 */

typedef struct LeakyBucket {
    uint64_t avg;             /* average goal in units per second */
    uint64_t max;             /* leaky bucket max burst in units */
    double  level;            /* bucket level in units */
    double  burst_level;      /* bucket level in units (for computing bursts) */
    uint64_t burst_length;    /* max length of the burst period, in seconds */
} LeakyBucket;

/* The following structure is used to configure a ThrottleState
 * It contains a bit of state: the bucket field of the LeakyBucket structure.
 * However it allows to keep the code clean and the bucket field is reset to
 * zero at the right time.
 */
typedef struct ThrottleConfig {
    LeakyBucket buckets[BUCKETS_COUNT]; /* leaky buckets */
    uint64_t op_size;         /* size of an operation in bytes */
} ThrottleConfig;

typedef struct ThrottleState {
    ThrottleConfig cfg;       /* configuration */
    int64_t previous_leak;    /* timestamp of the last leak done */
} ThrottleState;

typedef enum {
    THROTTLE_READ = 0,
    THROTTLE_WRITE,
    THROTTLE_MAX
} ThrottleDirection;

typedef struct ThrottleTimers {
    QEMUTimer *timers[THROTTLE_MAX];    /* timers used to do the throttling */
    QEMUClockType clock_type; /* the clock used */

    /* Callbacks */
    QEMUTimerCB *timer_cb[THROTTLE_MAX];
    void *timer_opaque;
} ThrottleTimers;

/* operations on single leaky buckets */
void throttle_leak_bucket(LeakyBucket *bkt, int64_t delta);

int64_t throttle_compute_wait(LeakyBucket *bkt);

/* init/destroy cycle */
void throttle_init(ThrottleState *ts);

void throttle_timers_init(ThrottleTimers *tt,
                          AioContext *aio_context,
                          QEMUClockType clock_type,
                          QEMUTimerCB *read_timer_cb,
                          QEMUTimerCB *write_timer_cb,
                          void *timer_opaque);

void throttle_timers_destroy(ThrottleTimers *tt);

void throttle_timers_detach_aio_context(ThrottleTimers *tt);

void throttle_timers_attach_aio_context(ThrottleTimers *tt,
                                        AioContext *new_context);

bool throttle_timers_are_initialized(ThrottleTimers *tt);

/* configuration */
bool throttle_enabled(ThrottleConfig *cfg);

bool throttle_is_valid(ThrottleConfig *cfg, Error **errp);

void throttle_config(ThrottleState *ts,
                     QEMUClockType clock_type,
                     ThrottleConfig *cfg);

void throttle_get_config(ThrottleState *ts, ThrottleConfig *cfg);

void throttle_config_init(ThrottleConfig *cfg);

/* usage */
bool throttle_schedule_timer(ThrottleState *ts,
                             ThrottleTimers *tt,
                             ThrottleDirection direction);

void throttle_account(ThrottleState *ts, ThrottleDirection direction,
                      uint64_t size);
void throttle_limits_to_config(ThrottleLimits *arg, ThrottleConfig *cfg,
                               Error **errp);
void throttle_config_to_limits(ThrottleConfig *cfg, ThrottleLimits *var);

#endif
