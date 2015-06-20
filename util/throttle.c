/*
 * QEMU throttling infrastructure
 *
 * Copyright (C) Nodalink, SARL. 2013
 *
 * Author:
 *   Benoît Canet <benoit.canet@irqsave.net>
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

#include "qemu/throttle.h"
#include "qemu/timer.h"

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

    extra = bkt->level - bkt->max;

    if (extra <= 0) {
        return 0;
    }

    return throttle_do_compute_wait(bkt->avg, extra);
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
bool throttle_compute_timer(ThrottleState *ts,
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

/* To be called first on the ThrottleState */
void throttle_init(ThrottleState *ts,
                   QEMUClockType clock_type,
                   QEMUTimerCB *read_timer_cb,
                   QEMUTimerCB *write_timer_cb,
                   void *timer_opaque)
{
    memset(ts, 0, sizeof(ThrottleState));

    ts->clock_type = clock_type;
    ts->timers[0] = timer_new_ns(clock_type, read_timer_cb, timer_opaque);
    ts->timers[1] = timer_new_ns(clock_type, write_timer_cb, timer_opaque);
}

/* destroy a timer */
static void throttle_timer_destroy(QEMUTimer **timer)
{
    assert(*timer != NULL);

    timer_del(*timer);
    timer_free(*timer);
    *timer = NULL;
}

/* To be called last on the ThrottleState */
void throttle_destroy(ThrottleState *ts)
{
    int i;

    for (i = 0; i < 2; i++) {
        throttle_timer_destroy(&ts->timers[i]);
    }
}

/* is any throttling timer configured */
bool throttle_have_timer(ThrottleState *ts)
{
    if (ts->timers[0]) {
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

/* return true if any two throttling parameters conflicts
 *
 * @cfg: the throttling configuration to inspect
 * @ret: true if any conflict detected else false
 */
bool throttle_conflicting(ThrottleConfig *cfg)
{
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

    return bps_flag || ops_flag || bps_max_flag || ops_max_flag;
}

/* check if a throttling configuration is valid
 * @cfg: the throttling configuration to inspect
 * @ret: true if valid else false
 */
bool throttle_is_valid(ThrottleConfig *cfg)
{
    bool invalid = false;
    int i;

    for (i = 0; i < BUCKETS_COUNT; i++) {
        if (cfg->buckets[i].avg < 0) {
            invalid = true;
        }
    }

    for (i = 0; i < BUCKETS_COUNT; i++) {
        if (cfg->buckets[i].max < 0) {
            invalid = true;
        }
    }

    return !invalid;
}

/* fix bucket parameters */
static void throttle_fix_bucket(LeakyBucket *bkt)
{
    double min;

    /* zero bucket level */
    bkt->level = 0;

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

/* take care of canceling a timer */
static void throttle_cancel_timer(QEMUTimer *timer)
{
    assert(timer != NULL);

    timer_del(timer);
}

/* Used to configure the throttle
 *
 * @ts: the throttle state we are working on
 * @cfg: the config to set
 */
void throttle_config(ThrottleState *ts, ThrottleConfig *cfg)
{
    int i;

    ts->cfg = *cfg;

    for (i = 0; i < BUCKETS_COUNT; i++) {
        throttle_fix_bucket(&ts->cfg.buckets[i]);
    }

    ts->previous_leak = qemu_clock_get_ns(ts->clock_type);

    for (i = 0; i < 2; i++) {
        throttle_cancel_timer(ts->timers[i]);
    }
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
 * @is_write: the type of operation (read/write)
 * @ret:      true if the timer has been scheduled else false
 */
bool throttle_schedule_timer(ThrottleState *ts, bool is_write)
{
    int64_t now = qemu_clock_get_ns(ts->clock_type);
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
    if (timer_pending(ts->timers[is_write])) {
        return true;
    }

    /* request throttled and timer not pending -> arm timer */
    timer_mod(ts->timers[is_write], next_timestamp);
    return true;
}

/* do the accounting for this operation
 *
 * @is_write: the type of operation (read/write)
 * @size:     the size of the operation
 */
void throttle_account(ThrottleState *ts, bool is_write, uint64_t size)
{
    double units = 1.0;

    /* if cfg.op_size is defined and smaller than size we compute unit count */
    if (ts->cfg.op_size && size > ts->cfg.op_size) {
        units = (double) size / ts->cfg.op_size;
    }

    ts->cfg.buckets[THROTTLE_BPS_TOTAL].level += size;
    ts->cfg.buckets[THROTTLE_OPS_TOTAL].level += units;

    if (is_write) {
        ts->cfg.buckets[THROTTLE_BPS_WRITE].level += size;
        ts->cfg.buckets[THROTTLE_OPS_WRITE].level += units;
    } else {
        ts->cfg.buckets[THROTTLE_BPS_READ].level += size;
        ts->cfg.buckets[THROTTLE_OPS_READ].level += units;
    }
}

