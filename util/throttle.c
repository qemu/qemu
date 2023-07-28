/*
 * QEMU throttling infrastructure
 *
 * Copyright (C) Nodalink, EURL. 2013-2014
 * Copyright (C) Igalia, S.L. 2015
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/throttle.h"
#include "qemu/timer.h"
#include "block/aio.h"

/* This function make a bucket leak
 *
 * @bkt:   the bucket to make leak
 * @delta_ns: the time delta
 */
void throttle_leak_bucket(LeakyBucket *bkt, int64_t delta_ns)
{
    double leak;

    /* compute how much to leak */
    leak = (bkt->avg * (double) delta_ns) / NANOSECONDS_PER_SECOND;

    /* make the bucket leak */
    bkt->level = MAX(bkt->level - leak, 0);

    /* if we allow bursts for more than one second we also need to
     * keep track of bkt->burst_level so the bkt->max goal per second
     * is attained */
    if (bkt->burst_length > 1) {
        leak = (bkt->max * (double) delta_ns) / NANOSECONDS_PER_SECOND;
        bkt->burst_level = MAX(bkt->burst_level - leak, 0);
    }
}

/* Calculate the time delta since last leak and make proportionals leaks
 *
 * @now:      the current timestamp in ns
 */
static void throttle_do_leak(ThrottleState *ts, int64_t now)
{
    /* compute the time elapsed since the last leak */
    int64_t delta_ns = now - ts->previous_leak;
    int i;

    ts->previous_leak = now;

    if (delta_ns <= 0) {
        return;
    }

    /* make each bucket leak */
    for (i = 0; i < BUCKETS_COUNT; i++) {
        throttle_leak_bucket(&ts->cfg.buckets[i], delta_ns);
    }
}

/* do the real job of computing the time to wait
 *
 * @limit: the throttling limit
 * @extra: the number of operation to delay
 * @ret:   the time to wait in ns
 */
static int64_t throttle_do_compute_wait(double limit, double extra)
{
    double wait = extra * NANOSECONDS_PER_SECOND;
    wait /= limit;
    return wait;
}

/* This function compute the wait time in ns that a leaky bucket should trigger
 *
 * @bkt: the leaky bucket we operate on
 * @ret: the resulting wait time in ns or 0 if the operation can go through
 */
int64_t throttle_compute_wait(LeakyBucket *bkt)
{
    double extra; /* the number of extra units blocking the io */
    double bucket_size;   /* I/O before throttling to bkt->avg */
    double burst_bucket_size; /* Before throttling to bkt->max */

    if (!bkt->avg) {
        return 0;
    }

    if (!bkt->max) {
        /* If bkt->max is 0 we still want to allow short bursts of I/O
         * from the guest, otherwise every other request will be throttled
         * and performance will suffer considerably. */
        bucket_size = (double) bkt->avg / 10;
        burst_bucket_size = 0;
    } else {
        /* If we have a burst limit then we have to wait until all I/O
         * at burst rate has finished before throttling to bkt->avg */
        bucket_size = bkt->max * bkt->burst_length;
        burst_bucket_size = (double) bkt->max / 10;
    }

    /* If the main bucket is full then we have to wait */
    extra = bkt->level - bucket_size;
    if (extra > 0) {
        return throttle_do_compute_wait(bkt->avg, extra);
    }

    /* If the main bucket is not full yet we still have to check the
     * burst bucket in order to enforce the burst limit */
    if (bkt->burst_length > 1) {
        assert(bkt->max > 0); /* see throttle_is_valid() */
        extra = bkt->burst_level - burst_bucket_size;
        if (extra > 0) {
            return throttle_do_compute_wait(bkt->max, extra);
        }
    }

    return 0;
}

/* This function compute the time that must be waited while this IO
 *
 * @direction:  throttle direction
 * @ret:        time to wait
 */
static int64_t throttle_compute_wait_for(ThrottleState *ts,
                                         ThrottleDirection direction)
{
    static const BucketType to_check[THROTTLE_MAX][4] = {
                                  {THROTTLE_BPS_TOTAL,
                                   THROTTLE_OPS_TOTAL,
                                   THROTTLE_BPS_READ,
                                   THROTTLE_OPS_READ},
                                  {THROTTLE_BPS_TOTAL,
                                   THROTTLE_OPS_TOTAL,
                                   THROTTLE_BPS_WRITE,
                                   THROTTLE_OPS_WRITE}, };
    int64_t wait, max_wait = 0;
    int i;

    for (i = 0; i < ARRAY_SIZE(to_check[THROTTLE_READ]); i++) {
        BucketType index = to_check[direction][i];
        wait = throttle_compute_wait(&ts->cfg.buckets[index]);
        if (wait > max_wait) {
            max_wait = wait;
        }
    }

    return max_wait;
}

/* compute the timer for this type of operation
 *
 * @direction:  throttle direction
 * @now:        the current clock timestamp
 * @next_timestamp: the resulting timer
 * @ret:        true if a timer must be set
 */
static bool throttle_compute_timer(ThrottleState *ts,
                                   ThrottleDirection direction,
                                   int64_t now,
                                   int64_t *next_timestamp)
{
    int64_t wait;

    /* leak proportionally to the time elapsed */
    throttle_do_leak(ts, now);

    /* compute the wait time if any */
    wait = throttle_compute_wait_for(ts, direction);

    /* if the code must wait compute when the next timer should fire */
    if (wait) {
        *next_timestamp = now + wait;
        return true;
    }

    /* else no need to wait at all */
    *next_timestamp = now;
    return false;
}

/* Add timers to event loop */
void throttle_timers_attach_aio_context(ThrottleTimers *tt,
                                        AioContext *new_context)
{
    ThrottleDirection dir;

    for (dir = THROTTLE_READ; dir < THROTTLE_MAX; dir++) {
        if (tt->timer_cb[dir]) {
            tt->timers[dir] =
                aio_timer_new(new_context, tt->clock_type, SCALE_NS,
                              tt->timer_cb[dir], tt->timer_opaque);
        }
    }
}

/*
 * Initialize the ThrottleConfig structure to a valid state
 * @cfg: the config to initialize
 */
void throttle_config_init(ThrottleConfig *cfg)
{
    unsigned i;
    memset(cfg, 0, sizeof(*cfg));
    for (i = 0; i < BUCKETS_COUNT; i++) {
        cfg->buckets[i].burst_length = 1;
    }
}

/* To be called first on the ThrottleState */
void throttle_init(ThrottleState *ts)
{
    memset(ts, 0, sizeof(ThrottleState));
    throttle_config_init(&ts->cfg);
}

/* To be called first on the ThrottleTimers */
void throttle_timers_init(ThrottleTimers *tt,
                          AioContext *aio_context,
                          QEMUClockType clock_type,
                          QEMUTimerCB *read_timer_cb,
                          QEMUTimerCB *write_timer_cb,
                          void *timer_opaque)
{
    assert(read_timer_cb || write_timer_cb);
    memset(tt, 0, sizeof(ThrottleTimers));

    tt->clock_type = clock_type;
    tt->timer_cb[THROTTLE_READ] = read_timer_cb;
    tt->timer_cb[THROTTLE_WRITE] = write_timer_cb;
    tt->timer_opaque = timer_opaque;
    throttle_timers_attach_aio_context(tt, aio_context);
}

/* destroy a timer */
static void throttle_timer_destroy(QEMUTimer **timer)
{
    if (*timer == NULL) {
        return;
    }

    timer_free(*timer);
    *timer = NULL;
}

/* Remove timers from event loop */
void throttle_timers_detach_aio_context(ThrottleTimers *tt)
{
    ThrottleDirection dir;

    for (dir = THROTTLE_READ; dir < THROTTLE_MAX; dir++) {
        throttle_timer_destroy(&tt->timers[dir]);
    }
}

/* To be called last on the ThrottleTimers */
void throttle_timers_destroy(ThrottleTimers *tt)
{
    throttle_timers_detach_aio_context(tt);
}

/* is any throttling timer configured */
bool throttle_timers_are_initialized(ThrottleTimers *tt)
{
    ThrottleDirection dir;

    for (dir = THROTTLE_READ; dir < THROTTLE_MAX; dir++) {
        if (tt->timers[dir]) {
            return true;
        }
    }

    return false;
}

/* Does any throttling must be done
 *
 * @cfg: the throttling configuration to inspect
 * @ret: true if throttling must be done else false
 */
bool throttle_enabled(ThrottleConfig *cfg)
{
    int i;

    for (i = 0; i < BUCKETS_COUNT; i++) {
        if (cfg->buckets[i].avg > 0) {
            return true;
        }
    }

    return false;
}

/* check if a throttling configuration is valid
 * @cfg: the throttling configuration to inspect
 * @ret: true if valid else false
 * @errp: error object
 */
bool throttle_is_valid(ThrottleConfig *cfg, Error **errp)
{
    int i;
    bool bps_flag, ops_flag;
    bool bps_max_flag, ops_max_flag;

    bps_flag = cfg->buckets[THROTTLE_BPS_TOTAL].avg &&
               (cfg->buckets[THROTTLE_BPS_READ].avg ||
                cfg->buckets[THROTTLE_BPS_WRITE].avg);

    ops_flag = cfg->buckets[THROTTLE_OPS_TOTAL].avg &&
               (cfg->buckets[THROTTLE_OPS_READ].avg ||
                cfg->buckets[THROTTLE_OPS_WRITE].avg);

    bps_max_flag = cfg->buckets[THROTTLE_BPS_TOTAL].max &&
                  (cfg->buckets[THROTTLE_BPS_READ].max  ||
                   cfg->buckets[THROTTLE_BPS_WRITE].max);

    ops_max_flag = cfg->buckets[THROTTLE_OPS_TOTAL].max &&
                   (cfg->buckets[THROTTLE_OPS_READ].max ||
                   cfg->buckets[THROTTLE_OPS_WRITE].max);

    if (bps_flag || ops_flag || bps_max_flag || ops_max_flag) {
        error_setg(errp, "bps/iops/max total values and read/write values"
                   " cannot be used at the same time");
        return false;
    }

    if (cfg->op_size &&
        !cfg->buckets[THROTTLE_OPS_TOTAL].avg &&
        !cfg->buckets[THROTTLE_OPS_READ].avg &&
        !cfg->buckets[THROTTLE_OPS_WRITE].avg) {
        error_setg(errp, "iops size requires an iops value to be set");
        return false;
    }

    for (i = 0; i < BUCKETS_COUNT; i++) {
        LeakyBucket *bkt = &cfg->buckets[i];
        if (bkt->avg > THROTTLE_VALUE_MAX || bkt->max > THROTTLE_VALUE_MAX) {
            error_setg(errp, "bps/iops/max values must be within [0, %lld]",
                       THROTTLE_VALUE_MAX);
            return false;
        }

        if (!bkt->burst_length) {
            error_setg(errp, "the burst length cannot be 0");
            return false;
        }

        if (bkt->burst_length > 1 && !bkt->max) {
            error_setg(errp, "burst length set without burst rate");
            return false;
        }

        if (bkt->max && bkt->burst_length > THROTTLE_VALUE_MAX / bkt->max) {
            error_setg(errp, "burst length too high for this burst rate");
            return false;
        }

        if (bkt->max && !bkt->avg) {
            error_setg(errp, "bps_max/iops_max require corresponding"
                       " bps/iops values");
            return false;
        }

        if (bkt->max && bkt->max < bkt->avg) {
            error_setg(errp, "bps_max/iops_max cannot be lower than bps/iops");
            return false;
        }
    }

    return true;
}

/* Used to configure the throttle
 *
 * @ts: the throttle state we are working on
 * @clock_type: the group's clock_type
 * @cfg: the config to set
 */
void throttle_config(ThrottleState *ts,
                     QEMUClockType clock_type,
                     ThrottleConfig *cfg)
{
    int i;

    ts->cfg = *cfg;

    /* Zero bucket level */
    for (i = 0; i < BUCKETS_COUNT; i++) {
        ts->cfg.buckets[i].level = 0;
        ts->cfg.buckets[i].burst_level = 0;
    }

    ts->previous_leak = qemu_clock_get_ns(clock_type);
}

/* used to get config
 *
 * @ts:  the throttle state we are working on
 * @cfg: the config to write
 */
void throttle_get_config(ThrottleState *ts, ThrottleConfig *cfg)
{
    *cfg = ts->cfg;
}


/* Schedule the read or write timer if needed
 *
 * NOTE: this function is not unit tested due to it's usage of timer_mod
 *
 * @tt:       the timers structure
 * @direction: throttle direction
 * @ret:      true if the timer has been scheduled else false
 */
bool throttle_schedule_timer(ThrottleState *ts,
                             ThrottleTimers *tt,
                             ThrottleDirection direction)
{
    int64_t now = qemu_clock_get_ns(tt->clock_type);
    int64_t next_timestamp;
    QEMUTimer *timer;
    bool must_wait;

    assert(direction < THROTTLE_MAX);
    timer = tt->timers[direction];
    assert(timer);

    must_wait = throttle_compute_timer(ts,
                                       direction,
                                       now,
                                       &next_timestamp);

    /* request not throttled */
    if (!must_wait) {
        return false;
    }

    /* request throttled and timer pending -> do nothing */
    if (timer_pending(timer)) {
        return true;
    }

    /* request throttled and timer not pending -> arm timer */
    timer_mod(timer, next_timestamp);
    return true;
}

/* do the accounting for this operation
 *
 * @direction: throttle direction
 * @size:     the size of the operation
 */
void throttle_account(ThrottleState *ts, ThrottleDirection direction,
                      uint64_t size)
{
    static const BucketType bucket_types_size[THROTTLE_MAX][2] = {
        { THROTTLE_BPS_TOTAL, THROTTLE_BPS_READ },
        { THROTTLE_BPS_TOTAL, THROTTLE_BPS_WRITE }
    };
    static const BucketType bucket_types_units[THROTTLE_MAX][2] = {
        { THROTTLE_OPS_TOTAL, THROTTLE_OPS_READ },
        { THROTTLE_OPS_TOTAL, THROTTLE_OPS_WRITE }
    };
    double units = 1.0;
    unsigned i;

    assert(direction < THROTTLE_MAX);
    /* if cfg.op_size is defined and smaller than size we compute unit count */
    if (ts->cfg.op_size && size > ts->cfg.op_size) {
        units = (double) size / ts->cfg.op_size;
    }

    for (i = 0; i < ARRAY_SIZE(bucket_types_size[THROTTLE_READ]); i++) {
        LeakyBucket *bkt;

        bkt = &ts->cfg.buckets[bucket_types_size[direction][i]];
        bkt->level += size;
        if (bkt->burst_length > 1) {
            bkt->burst_level += size;
        }

        bkt = &ts->cfg.buckets[bucket_types_units[direction][i]];
        bkt->level += units;
        if (bkt->burst_length > 1) {
            bkt->burst_level += units;
        }
    }
}

/* return a ThrottleConfig based on the options in a ThrottleLimits
 *
 * @arg:    the ThrottleLimits object to read from
 * @cfg:    the ThrottleConfig to edit
 * @errp:   error object
 */
void throttle_limits_to_config(ThrottleLimits *arg, ThrottleConfig *cfg,
                               Error **errp)
{
    if (arg->has_bps_total) {
        cfg->buckets[THROTTLE_BPS_TOTAL].avg = arg->bps_total;
    }
    if (arg->has_bps_read) {
        cfg->buckets[THROTTLE_BPS_READ].avg  = arg->bps_read;
    }
    if (arg->has_bps_write) {
        cfg->buckets[THROTTLE_BPS_WRITE].avg = arg->bps_write;
    }

    if (arg->has_iops_total) {
        cfg->buckets[THROTTLE_OPS_TOTAL].avg = arg->iops_total;
    }
    if (arg->has_iops_read) {
        cfg->buckets[THROTTLE_OPS_READ].avg  = arg->iops_read;
    }
    if (arg->has_iops_write) {
        cfg->buckets[THROTTLE_OPS_WRITE].avg = arg->iops_write;
    }

    if (arg->has_bps_total_max) {
        cfg->buckets[THROTTLE_BPS_TOTAL].max = arg->bps_total_max;
    }
    if (arg->has_bps_read_max) {
        cfg->buckets[THROTTLE_BPS_READ].max = arg->bps_read_max;
    }
    if (arg->has_bps_write_max) {
        cfg->buckets[THROTTLE_BPS_WRITE].max = arg->bps_write_max;
    }
    if (arg->has_iops_total_max) {
        cfg->buckets[THROTTLE_OPS_TOTAL].max = arg->iops_total_max;
    }
    if (arg->has_iops_read_max) {
        cfg->buckets[THROTTLE_OPS_READ].max = arg->iops_read_max;
    }
    if (arg->has_iops_write_max) {
        cfg->buckets[THROTTLE_OPS_WRITE].max = arg->iops_write_max;
    }

    if (arg->has_bps_total_max_length) {
        if (arg->bps_total_max_length > UINT_MAX) {
            error_setg(errp, "bps-total-max-length value must be in"
                             " the range [0, %u]", UINT_MAX);
            return;
        }
        cfg->buckets[THROTTLE_BPS_TOTAL].burst_length = arg->bps_total_max_length;
    }
    if (arg->has_bps_read_max_length) {
        if (arg->bps_read_max_length > UINT_MAX) {
            error_setg(errp, "bps-read-max-length value must be in"
                             " the range [0, %u]", UINT_MAX);
            return;
        }
        cfg->buckets[THROTTLE_BPS_READ].burst_length = arg->bps_read_max_length;
    }
    if (arg->has_bps_write_max_length) {
        if (arg->bps_write_max_length > UINT_MAX) {
            error_setg(errp, "bps-write-max-length value must be in"
                             " the range [0, %u]", UINT_MAX);
            return;
        }
        cfg->buckets[THROTTLE_BPS_WRITE].burst_length = arg->bps_write_max_length;
    }
    if (arg->has_iops_total_max_length) {
        if (arg->iops_total_max_length > UINT_MAX) {
            error_setg(errp, "iops-total-max-length value must be in"
                             " the range [0, %u]", UINT_MAX);
            return;
        }
        cfg->buckets[THROTTLE_OPS_TOTAL].burst_length = arg->iops_total_max_length;
    }
    if (arg->has_iops_read_max_length) {
        if (arg->iops_read_max_length > UINT_MAX) {
            error_setg(errp, "iops-read-max-length value must be in"
                             " the range [0, %u]", UINT_MAX);
            return;
        }
        cfg->buckets[THROTTLE_OPS_READ].burst_length = arg->iops_read_max_length;
    }
    if (arg->has_iops_write_max_length) {
        if (arg->iops_write_max_length > UINT_MAX) {
            error_setg(errp, "iops-write-max-length value must be in"
                             " the range [0, %u]", UINT_MAX);
            return;
        }
        cfg->buckets[THROTTLE_OPS_WRITE].burst_length = arg->iops_write_max_length;
    }

    if (arg->has_iops_size) {
        cfg->op_size = arg->iops_size;
    }

    throttle_is_valid(cfg, errp);
}

/* write the options of a ThrottleConfig to a ThrottleLimits
 *
 * @cfg:    the ThrottleConfig to read from
 * @var:    the ThrottleLimits to write to
 */
void throttle_config_to_limits(ThrottleConfig *cfg, ThrottleLimits *var)
{
    var->bps_total               = cfg->buckets[THROTTLE_BPS_TOTAL].avg;
    var->bps_read                = cfg->buckets[THROTTLE_BPS_READ].avg;
    var->bps_write               = cfg->buckets[THROTTLE_BPS_WRITE].avg;
    var->iops_total              = cfg->buckets[THROTTLE_OPS_TOTAL].avg;
    var->iops_read               = cfg->buckets[THROTTLE_OPS_READ].avg;
    var->iops_write              = cfg->buckets[THROTTLE_OPS_WRITE].avg;
    var->bps_total_max           = cfg->buckets[THROTTLE_BPS_TOTAL].max;
    var->bps_read_max            = cfg->buckets[THROTTLE_BPS_READ].max;
    var->bps_write_max           = cfg->buckets[THROTTLE_BPS_WRITE].max;
    var->iops_total_max          = cfg->buckets[THROTTLE_OPS_TOTAL].max;
    var->iops_read_max           = cfg->buckets[THROTTLE_OPS_READ].max;
    var->iops_write_max          = cfg->buckets[THROTTLE_OPS_WRITE].max;
    var->bps_total_max_length    = cfg->buckets[THROTTLE_BPS_TOTAL].burst_length;
    var->bps_read_max_length     = cfg->buckets[THROTTLE_BPS_READ].burst_length;
    var->bps_write_max_length    = cfg->buckets[THROTTLE_BPS_WRITE].burst_length;
    var->iops_total_max_length   = cfg->buckets[THROTTLE_OPS_TOTAL].burst_length;
    var->iops_read_max_length    = cfg->buckets[THROTTLE_OPS_READ].burst_length;
    var->iops_write_max_length   = cfg->buckets[THROTTLE_OPS_WRITE].burst_length;
    var->iops_size               = cfg->op_size;

    var->has_bps_total = true;
    var->has_bps_read = true;
    var->has_bps_write = true;
    var->has_iops_total = true;
    var->has_iops_read = true;
    var->has_iops_write = true;
    var->has_bps_total_max = true;
    var->has_bps_read_max = true;
    var->has_bps_write_max = true;
    var->has_iops_total_max = true;
    var->has_iops_read_max = true;
    var->has_iops_write_max = true;
    var->has_bps_read_max_length = true;
    var->has_bps_total_max_length = true;
    var->has_bps_write_max_length = true;
    var->has_iops_total_max_length = true;
    var->has_iops_read_max_length = true;
    var->has_iops_write_max_length = true;
    var->has_iops_size = true;
}
