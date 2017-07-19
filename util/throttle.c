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

    if (!bkt->avg) {
        return 0;
    }

    /* If the bucket is full then we have to wait */
    extra = bkt->level - bkt->max * bkt->burst_length;
    if (extra > 0) {
        return throttle_do_compute_wait(bkt->avg, extra);
    }

    /* If the bucket is not full yet we have to make sure that we
     * fulfill the goal of bkt->max units per second. */
    if (bkt->burst_length > 1) {
        /* We use 1/10 of the max value to smooth the throttling.
         * See throttle_fix_bucket() for more details. */
        extra = bkt->burst_level - bkt->max / 10;
        if (extra > 0) {
            return throttle_do_compute_wait(bkt->max, extra);
        }
    }

    return 0;
}

/* This function compute the time that must be waited while this IO
 *
 * @is_write:   true if the current IO is a write, false if it's a read
 * @ret:        time to wait
 */
static int64_t throttle_compute_wait_for(ThrottleState *ts,
                                         bool is_write)
{
    BucketType to_check[2][4] = { {THROTTLE_BPS_TOTAL,
                                   THROTTLE_OPS_TOTAL,
                                   THROTTLE_BPS_READ,
                                   THROTTLE_OPS_READ},
                                  {THROTTLE_BPS_TOTAL,
                                   THROTTLE_OPS_TOTAL,
                                   THROTTLE_BPS_WRITE,
                                   THROTTLE_OPS_WRITE}, };
    int64_t wait, max_wait = 0;
    int i;

    for (i = 0; i < 4; i++) {
        BucketType index = to_check[is_write][i];
        wait = throttle_compute_wait(&ts->cfg.buckets[index]);
        if (wait > max_wait) {
            max_wait = wait;
        }
    }

    return max_wait;
}

/* compute the timer for this type of operation
 *
 * @is_write:   the type of operation
 * @now:        the current clock timestamp
 * @next_timestamp: the resulting timer
 * @ret:        true if a timer must be set
 */
static bool throttle_compute_timer(ThrottleState *ts,
                                   bool is_write,
                                   int64_t now,
                                   int64_t *next_timestamp)
{
    int64_t wait;

    /* leak proportionally to the time elapsed */
    throttle_do_leak(ts, now);

    /* compute the wait time if any */
    wait = throttle_compute_wait_for(ts, is_write);

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
    tt->timers[0] = aio_timer_new(new_context, tt->clock_type, SCALE_NS,
                                  tt->read_timer_cb, tt->timer_opaque);
    tt->timers[1] = aio_timer_new(new_context, tt->clock_type, SCALE_NS,
                                  tt->write_timer_cb, tt->timer_opaque);
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
    memset(tt, 0, sizeof(ThrottleTimers));

    tt->clock_type = clock_type;
    tt->read_timer_cb = read_timer_cb;
    tt->write_timer_cb = write_timer_cb;
    tt->timer_opaque = timer_opaque;
    throttle_timers_attach_aio_context(tt, aio_context);
}

/* destroy a timer */
static void throttle_timer_destroy(QEMUTimer **timer)
{
    assert(*timer != NULL);

    timer_del(*timer);
    timer_free(*timer);
    *timer = NULL;
}

/* Remove timers from event loop */
void throttle_timers_detach_aio_context(ThrottleTimers *tt)
{
    int i;

    for (i = 0; i < 2; i++) {
        throttle_timer_destroy(&tt->timers[i]);
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
    if (tt->timers[0]) {
        return true;
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
        if (cfg->buckets[i].avg < 0 ||
            cfg->buckets[i].max < 0 ||
            cfg->buckets[i].avg > THROTTLE_VALUE_MAX ||
            cfg->buckets[i].max > THROTTLE_VALUE_MAX) {
            error_setg(errp, "bps/iops/max values must be within [0, %lld]",
                       THROTTLE_VALUE_MAX);
            return false;
        }

        if (!cfg->buckets[i].burst_length) {
            error_setg(errp, "the burst length cannot be 0");
            return false;
        }

        if (cfg->buckets[i].burst_length > 1 && !cfg->buckets[i].max) {
            error_setg(errp, "burst length set without burst rate");
            return false;
        }

        if (cfg->buckets[i].max && !cfg->buckets[i].avg) {
            error_setg(errp, "bps_max/iops_max require corresponding"
                       " bps/iops values");
            return false;
        }

        if (cfg->buckets[i].max && cfg->buckets[i].max < cfg->buckets[i].avg) {
            error_setg(errp, "bps_max/iops_max cannot be lower than bps/iops");
            return false;
        }
    }

    return true;
}

/* fix bucket parameters */
static void throttle_fix_bucket(LeakyBucket *bkt)
{
    double min;

    /* zero bucket level */
    bkt->level = bkt->burst_level = 0;

    /* The following is done to cope with the Linux CFQ block scheduler
     * which regroup reads and writes by block of 100ms in the guest.
     * When they are two process one making reads and one making writes cfq
     * make a pattern looking like the following:
     * WWWWWWWWWWWRRRRRRRRRRRRRRWWWWWWWWWWWWWwRRRRRRRRRRRRRRRRR
     * Having a max burst value of 100ms of the average will help smooth the
     * throttling
     */
    min = bkt->avg / 10;
    if (bkt->avg && !bkt->max) {
        bkt->max = min;
    }
}

/* undo internal bucket parameter changes (see throttle_fix_bucket()) */
static void throttle_unfix_bucket(LeakyBucket *bkt)
{
    if (bkt->max < bkt->avg) {
        bkt->max = 0;
    }
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

    for (i = 0; i < BUCKETS_COUNT; i++) {
        throttle_fix_bucket(&ts->cfg.buckets[i]);
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
    int i;

    *cfg = ts->cfg;

    for (i = 0; i < BUCKETS_COUNT; i++) {
        throttle_unfix_bucket(&cfg->buckets[i]);
    }
}


/* Schedule the read or write timer if needed
 *
 * NOTE: this function is not unit tested due to it's usage of timer_mod
 *
 * @tt:       the timers structure
 * @is_write: the type of operation (read/write)
 * @ret:      true if the timer has been scheduled else false
 */
bool throttle_schedule_timer(ThrottleState *ts,
                             ThrottleTimers *tt,
                             bool is_write)
{
    int64_t now = qemu_clock_get_ns(tt->clock_type);
    int64_t next_timestamp;
    bool must_wait;

    must_wait = throttle_compute_timer(ts,
                                       is_write,
                                       now,
                                       &next_timestamp);

    /* request not throttled */
    if (!must_wait) {
        return false;
    }

    /* request throttled and timer pending -> do nothing */
    if (timer_pending(tt->timers[is_write])) {
        return true;
    }

    /* request throttled and timer not pending -> arm timer */
    timer_mod(tt->timers[is_write], next_timestamp);
    return true;
}

/* do the accounting for this operation
 *
 * @is_write: the type of operation (read/write)
 * @size:     the size of the operation
 */
void throttle_account(ThrottleState *ts, bool is_write, uint64_t size)
{
    const BucketType bucket_types_size[2][2] = {
        { THROTTLE_BPS_TOTAL, THROTTLE_BPS_READ },
        { THROTTLE_BPS_TOTAL, THROTTLE_BPS_WRITE }
    };
    const BucketType bucket_types_units[2][2] = {
        { THROTTLE_OPS_TOTAL, THROTTLE_OPS_READ },
        { THROTTLE_OPS_TOTAL, THROTTLE_OPS_WRITE }
    };
    double units = 1.0;
    unsigned i;

    /* if cfg.op_size is defined and smaller than size we compute unit count */
    if (ts->cfg.op_size && size > ts->cfg.op_size) {
        units = (double) size / ts->cfg.op_size;
    }

    for (i = 0; i < 2; i++) {
        LeakyBucket *bkt;

        bkt = &ts->cfg.buckets[bucket_types_size[is_write][i]];
        bkt->level += size;
        if (bkt->burst_length > 1) {
            bkt->burst_level += size;
        }

        bkt = &ts->cfg.buckets[bucket_types_units[is_write][i]];
        bkt->level += units;
        if (bkt->burst_length > 1) {
            bkt->burst_level += units;
        }
    }
}

