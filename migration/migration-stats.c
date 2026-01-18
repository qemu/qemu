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
#include "qemu/atomic.h"
#include "qemu-file.h"
#include "trace.h"
#include "migration-stats.h"

MigrationAtomicStats mig_stats;

bool migration_rate_exceeded(QEMUFile *f)
{
    if (qemu_file_get_error(f)) {
        return true;
    }

    uint64_t rate_limit_max = migration_rate_get();
    if (rate_limit_max == RATE_LIMIT_DISABLED) {
        return false;
    }

    uint64_t rate_limit_start = qatomic_read(&mig_stats.rate_limit_start);
    uint64_t rate_limit_current = migration_transferred_bytes();
    uint64_t rate_limit_used = rate_limit_current - rate_limit_start;

    if (rate_limit_max > 0 && rate_limit_used > rate_limit_max) {
        return true;
    }
    return false;
}

uint64_t migration_rate_get(void)
{
    return qatomic_read(&mig_stats.rate_limit_max);
}

#define XFER_LIMIT_RATIO (1000 / BUFFER_DELAY)

void migration_rate_set(uint64_t limit)
{
    /*
     * 'limit' is per second.  But we check it each BUFFER_DELAY milliseconds.
     */
    qatomic_set(&mig_stats.rate_limit_max, limit / XFER_LIMIT_RATIO);
}

void migration_rate_reset(void)
{
    qatomic_set(&mig_stats.rate_limit_start, migration_transferred_bytes());
}

uint64_t migration_transferred_bytes(void)
{
    uint64_t multifd = qatomic_read(&mig_stats.multifd_bytes);
    uint64_t rdma = qatomic_read(&mig_stats.rdma_bytes);
    uint64_t qemu_file = qatomic_read(&mig_stats.qemu_file_transferred);

    trace_migration_transferred_bytes(qemu_file, multifd, rdma);
    return qemu_file + multifd + rdma;
}
