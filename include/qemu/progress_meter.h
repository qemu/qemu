/*
 * Helper functionality for some process progress tracking.
 *
 * Copyright (c) 2011 IBM Corp.
 * Copyright (c) 2012, 2018 Red Hat, Inc.
 * Copyright (c) 2020 Virtuozzo International GmbH
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef QEMU_PROGRESS_METER_H
#define QEMU_PROGRESS_METER_H

#include "qemu/thread.h"

typedef struct ProgressMeter {
    /**
     * Current progress. The unit is arbitrary as long as the ratio between
     * current and total represents the estimated percentage
     * of work already done.
     */
    uint64_t current;

    /** Estimated current value at the completion of the process */
    uint64_t total;

    QemuMutex lock; /* protects concurrent access to above fields */
} ProgressMeter;

void progress_init(ProgressMeter *pm);
void progress_destroy(ProgressMeter *pm);

/* Get a snapshot of internal current and total values  */
void progress_get_snapshot(ProgressMeter *pm, uint64_t *current,
                           uint64_t *total);

/* Increases the amount of work done so far by @done */
void progress_work_done(ProgressMeter *pm, uint64_t done);

/* Sets how much work has to be done to complete to @remaining */
void progress_set_remaining(ProgressMeter *pm, uint64_t remaining);

/* Increases the total work to do by @delta */
void progress_increase_remaining(ProgressMeter *pm, uint64_t delta);

#endif /* QEMU_PROGRESS_METER_H */
