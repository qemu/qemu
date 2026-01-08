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

/*
 * Amount of time to allocate to each "chunk" of bandwidth-throttled
 * data.
 */
#define BUFFER_DELAY     100

/*
 * If rate_limit_max is 0, there is special code to remove the rate
 * limit.
 */
#define RATE_LIMIT_DISABLED 0

/*
 * These are the ram migration statistic counters.  It is loosely
 * based on MigrationStats.
 */
typedef struct {
    /*
     * Number of bytes that were dirty last time that we synced with
     * the guest memory.  We use that to calculate the downtime.  As
     * the remaining dirty amounts to what we know that is still dirty
     * since last iteration, not counting what the guest has dirtied
     * since we synchronized bitmaps.
     */
    uint64_t dirty_bytes_last_sync;
    /*
     * Number of pages dirtied per second.
     */
    uint64_t dirty_pages_rate;
    /*
     * Number of times we have synchronized guest bitmaps.
     */
    uint64_t dirty_sync_count;
    /*
     * Number of times zero copy failed to send any page using zero
     * copy.
     */
    uint64_t dirty_sync_missed_zero_copy;
    /*
     * Number of bytes sent at migration completion stage while the
     * guest is stopped.
     */
    uint64_t downtime_bytes;
    /*
     * Number of bytes sent through multifd channels.
     */
    uint64_t multifd_bytes;
    /*
     * Number of pages transferred that were not full of zeros.
     */
    uint64_t normal_pages;
    /*
     * Number of bytes sent during postcopy.
     */
    uint64_t postcopy_bytes;
    /*
     * Number of postcopy page faults that we have handled during
     * postcopy stage.
     */
    uint64_t postcopy_requests;
    /*
     * Number of bytes sent during precopy stage.
     */
    uint64_t precopy_bytes;
    /*
     * Number of bytes transferred with QEMUFile.
     */
    uint64_t qemu_file_transferred;
    /*
     * Amount of transferred data at the start of current cycle.
     */
    uint64_t rate_limit_start;
    /*
     * Maximum amount of data we can send in a cycle.
     */
    uint64_t rate_limit_max;
    /*
     * Number of bytes sent through RDMA.
     */
    uint64_t rdma_bytes;
    /*
     * Number of pages transferred that were full of zeros.
     */
    uint64_t zero_pages;
} MigrationAtomicStats;

extern MigrationAtomicStats mig_stats;

/**
 * migration_rate_get: Get the maximum amount that can be transferred.
 *
 * Returns the maximum number of bytes that can be transferred in a cycle.
 */
uint64_t migration_rate_get(void);

/**
 * migration_rate_reset: Reset the rate limit counter.
 *
 * This is called when we know we start a new transfer cycle.
 */
void migration_rate_reset(void);

/**
 * migration_rate_set: Set the maximum amount that can be transferred.
 *
 * Sets the maximum amount of bytes that can be transferred in one cycle.
 *
 * @new_rate: new maximum amount
 */
void migration_rate_set(uint64_t new_rate);

/**
 * migration_transferred_bytes: Return number of bytes transferred
 *
 * Returns how many bytes have we transferred since the beginning of
 * the migration.  It accounts for bytes sent through any migration
 * channel, multifd, qemu_file, rdma, ....
 */
uint64_t migration_transferred_bytes(void);
#endif
