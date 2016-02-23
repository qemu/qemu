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

#ifndef TIMED_AVERAGE_H
#define TIMED_AVERAGE_H


#include "qemu/timer.h"

typedef struct TimedAverageWindow TimedAverageWindow;
typedef struct TimedAverage TimedAverage;

/* All fields of both structures are private */

struct TimedAverageWindow {
    uint64_t      min;             /* minimum value accounted in the window */
    uint64_t      max;             /* maximum value accounted in the window */
    uint64_t      sum;             /* sum of all values */
    uint64_t      count;           /* number of values */
    int64_t       expiration;      /* the end of the current window in ns */
};

struct TimedAverage {
    uint64_t           period;     /* period in nanoseconds */
    TimedAverageWindow windows[2]; /* two overlapping windows of with
                                    * an offset of period / 2 between them */
    unsigned           current;    /* the current window index: it's also the
                                    * oldest window index */
    QEMUClockType      clock_type; /* the clock used */
};

void timed_average_init(TimedAverage *ta, QEMUClockType clock_type,
                        uint64_t period);

void timed_average_account(TimedAverage *ta, uint64_t value);

uint64_t timed_average_min(TimedAverage *ta);
uint64_t timed_average_avg(TimedAverage *ta);
uint64_t timed_average_max(TimedAverage *ta);
uint64_t timed_average_sum(TimedAverage *ta, uint64_t *elapsed);

#endif
