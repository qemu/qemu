/*
 * QEMU timed average computation
 *
 * Copyright (C) Nodalink, EURL. 2014
 * Copyright (C) Igalia, S.L. 2015
 *
 * Authors:
 *   Benoît Canet <benoit.canet@nodalink.com>
 *   Alberto Garcia <berto@igalia.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) version 3 or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include "qemu/timed-average.h"

/* This module computes an average of a set of values within a time
 * window.
 *
 * Algorithm:
 *
 * - Create two windows with a certain expiration period, and
 *   offsetted by period / 2.
 * - Each time you want to account a new value, do it in both windows.
 * - The minimum / maximum / average values are always returned from
 *   the oldest window.
 *
 * Example:
 *
 *        t=0          |t=0.5           |t=1          |t=1.5            |t=2
 *        wnd0: [0,0.5)|wnd0: [0.5,1.5) |             |wnd0: [1.5,2.5)  |
 *        wnd1: [0,1)  |                |wnd1: [1,2)  |                 |
 *
 * Values are returned from:
 *
 *        wnd0---------|wnd1------------|wnd0---------|wnd1-------------|
 */

/* Update the expiration of a time window
 *
 * @w:      the window used
 * @now:    the current time in nanoseconds
 * @period: the expiration period in nanoseconds
 */
static void update_expiration(TimedAverageWindow *w, int64_t now,
                              int64_t period)
{
    /* time elapsed since the last theoretical expiration */
    int64_t elapsed = (now - w->expiration) % period;
    /* time remaininging until the next expiration */
    int64_t remaining = period - elapsed;
    /* compute expiration */
    w->expiration = now + remaining;
}

/* Reset a window
 *
 * @w: the window to reset
 */
static void window_reset(TimedAverageWindow *w)
{
    w->min = UINT64_MAX;
    w->max = 0;
    w->sum = 0;
    w->count = 0;
}

/* Get the current window (that is, the one with the earliest
 * expiration time).
 *
 * @ta:  the TimedAverage structure
 * @ret: a pointer to the current window
 */
static TimedAverageWindow *current_window(TimedAverage *ta)
{
     return &ta->windows[ta->current];
}

/* Initialize a TimedAverage structure
 *
 * @ta:         the TimedAverage structure
 * @clock_type: the type of clock to use
 * @period:     the time window period in nanoseconds
 */
void timed_average_init(TimedAverage *ta, QEMUClockType clock_type,
                        uint64_t period)
{
    int64_t now = qemu_clock_get_ns(clock_type);

    /* Returned values are from the oldest window, so they belong to
     * the interval [ta->period/2,ta->period). By adjusting the
     * requested period by 4/3, we guarantee that they're in the
     * interval [2/3 period,4/3 period), closer to the requested
     * period on average */
    ta->period = (uint64_t) period * 4 / 3;
    ta->clock_type = clock_type;
    ta->current = 0;

    window_reset(&ta->windows[0]);
    window_reset(&ta->windows[1]);

    /* Both windows are offsetted by half a period */
    ta->windows[0].expiration = now + ta->period / 2;
    ta->windows[1].expiration = now + ta->period;
}

/* Check if the time windows have expired, updating their counters and
 * expiration time if that's the case.
 *
 * @ta: the TimedAverage structure
 * @elapsed: if non-NULL, the elapsed time (in ns) within the current
 *           window will be stored here
 */
static void check_expirations(TimedAverage *ta, uint64_t *elapsed)
{
    int64_t now = qemu_clock_get_ns(ta->clock_type);
    int i;

    assert(ta->period != 0);

    /* Check if the windows have expired */
    for (i = 0; i < 2; i++) {
        TimedAverageWindow *w = &ta->windows[i];
        if (w->expiration <= now) {
            window_reset(w);
            update_expiration(w, now, ta->period);
        }
    }

    /* Make ta->current point to the oldest window */
    if (ta->windows[0].expiration < ta->windows[1].expiration) {
        ta->current = 0;
    } else {
        ta->current = 1;
    }

    /* Calculate the elapsed time within the current window */
    if (elapsed) {
        int64_t remaining = ta->windows[ta->current].expiration - now;
        *elapsed = ta->period - remaining;
    }
}

/* Account a value
 *
 * @ta:    the TimedAverage structure
 * @value: the value to account
 */
void timed_average_account(TimedAverage *ta, uint64_t value)
{
    int i;
    check_expirations(ta, NULL);

    /* Do the accounting in both windows at the same time */
    for (i = 0; i < 2; i++) {
        TimedAverageWindow *w = &ta->windows[i];

        w->sum += value;
        w->count++;

        if (value < w->min) {
            w->min = value;
        }

        if (value > w->max) {
            w->max = value;
        }
    }
}

/* Get the minimum value
 *
 * @ta:  the TimedAverage structure
 * @ret: the minimum value
 */
uint64_t timed_average_min(TimedAverage *ta)
{
    TimedAverageWindow *w;
    check_expirations(ta, NULL);
    w = current_window(ta);
    return w->min < UINT64_MAX ? w->min : 0;
}

/* Get the average value
 *
 * @ta:  the TimedAverage structure
 * @ret: the average value
 */
uint64_t timed_average_avg(TimedAverage *ta)
{
    TimedAverageWindow *w;
    check_expirations(ta, NULL);
    w = current_window(ta);
    return w->count > 0 ? w->sum / w->count : 0;
}

/* Get the maximum value
 *
 * @ta:  the TimedAverage structure
 * @ret: the maximum value
 */
uint64_t timed_average_max(TimedAverage *ta)
{
    check_expirations(ta, NULL);
    return current_window(ta)->max;
}

/* Get the sum of all accounted values
 * @ta:      the TimedAverage structure
 * @elapsed: if non-NULL, the elapsed time (in ns) will be stored here
 * @ret:     the sum of all accounted values
 */
uint64_t timed_average_sum(TimedAverage *ta, uint64_t *elapsed)
{
    TimedAverageWindow *w;
    check_expirations(ta, elapsed);
    w = current_window(ta);
    return w->sum;
}
