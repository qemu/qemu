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

#include "qemu/osdep.h"
#include "qemu/stats64.h"
#include "qemu-file.h"
#include "trace.h"
#include "migration-stats.h"

MigrationAtomicStats mig_stats;

bool migration_rate_exceeded(QEMUFile *f)
{
    if (qemu_file_get_error(f)) {
        return true;
    }

    uint64_t rate_limit_start = stat64_get(&mig_stats.rate_limit_start);
    uint64_t rate_limit_current = migration_transferred_bytes(f);
    uint64_t rate_limit_used = rate_limit_current - rate_limit_start;
    uint64_t rate_limit_max = stat64_get(&mig_stats.rate_limit_max);

    if (rate_limit_max == RATE_LIMIT_DISABLED) {
        return false;
    }
    if (rate_limit_max > 0 && rate_limit_used > rate_limit_max) {
        return true;
    }
    return false;
}

uint64_t migration_rate_get(void)
{
    return stat64_get(&mig_stats.rate_limit_max);
}

#define XFER_LIMIT_RATIO (1000 / BUFFER_DELAY)

void migration_rate_set(uint64_t limit)
{
    /*
     * 'limit' is per second.  But we check it each BUFER_DELAY miliseconds.
     */
    stat64_set(&mig_stats.rate_limit_max, limit / XFER_LIMIT_RATIO);
}

void migration_rate_reset(QEMUFile *f)
{
    stat64_set(&mig_stats.rate_limit_start, migration_transferred_bytes(f));
}

uint64_t migration_transferred_bytes(QEMUFile *f)
{
    uint64_t multifd = stat64_get(&mig_stats.multifd_bytes);
    uint64_t qemu_file = qemu_file_transferred(f);

    trace_migration_transferred_bytes(qemu_file, multifd);
    return qemu_file + multifd;
}
