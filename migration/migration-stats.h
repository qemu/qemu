/*
 * Migration stats
 *
 * Copyright (c) 2012-2023 Red Hat Inc
 *
 * Authors:
 *  Juan Quintela <quintela@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_MIGRATION_STATS_H
#define QEMU_MIGRATION_STATS_H

#include "qemu/stats64.h"

/*
 * If rate_limit_max is 0, there is special code to remove the rate
 * limit.
 */
#define RATE_LIMIT_DISABLED 0

/*
 * These are the ram migration statistic counters.  It is loosely
 * based on MigrationStats.  We change to Stat64 any counter that
 * needs to be updated using atomic ops (can be accessed by more than
 * one thread).
 */
typedef struct {
    /*
     * Number of bytes that were dirty last time that we synced with
     * the guest memory.  We use that to calculate the downtime.  As
     * the remaining dirty amounts to what we know that is still dirty
     * since last iteration, not counting what the guest has dirtied
     * since we synchronized bitmaps.
     */
    Stat64 dirty_bytes_last_sync;
    /*
     * Number of pages dirtied per second.
     */
    Stat64 dirty_pages_rate;
    /*
     * Number of times we have synchronized guest bitmaps.
     */
    Stat64 dirty_sync_count;
    /*
     * Number of times zero copy failed to send any page using zero
     * copy.
     */
    Stat64 dirty_sync_missed_zero_copy;
    /*
     * Number of bytes sent at migration completion stage while the
     * guest is stopped.
     */
    Stat64 downtime_bytes;
    /*
     * Number of bytes sent through multifd channels.
     */
    Stat64 multifd_bytes;
    /*
     * Number of pages transferred that were not full of zeros.
     */
    Stat64 normal_pages;
    /*
     * Number of bytes sent during postcopy.
     */
    Stat64 postcopy_bytes;
    /*
     * Number of postcopy page faults that we have handled during
     * postcopy stage.
     */
    Stat64 postcopy_requests;
    /*
     * Number of bytes sent during precopy stage.
     */
    Stat64 precopy_bytes;
    /*
     * Total number of bytes transferred.
     */
    Stat64 transferred;
    /*
     * Number of pages transferred that were full of zeros.
     */
    Stat64 zero_pages;
} MigrationAtomicStats;

extern MigrationAtomicStats mig_stats;

#endif
