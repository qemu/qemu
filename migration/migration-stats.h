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
 * These are the ram migration statistic counters.  It is loosely
 * based on MigrationStats.  We change to Stat64 any counter that
 * needs to be updated using atomic ops (can be accessed by more than
 * one thread).
 */
typedef struct {
    Stat64 dirty_bytes_last_sync;
    Stat64 dirty_pages_rate;
    Stat64 dirty_sync_count;
    Stat64 dirty_sync_missed_zero_copy;
    Stat64 downtime_bytes;
    Stat64 zero_pages;
    Stat64 multifd_bytes;
    Stat64 normal_pages;
    Stat64 postcopy_bytes;
    Stat64 postcopy_requests;
    Stat64 precopy_bytes;
    Stat64 transferred;
} MigrationAtomicStats;

extern MigrationAtomicStats mig_stats;

#endif
