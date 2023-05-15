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
#include "migration-stats.h"

MigrationAtomicStats mig_stats;

bool migration_rate_exceeded(QEMUFile *f)
{
    if (qemu_file_get_error(f)) {
        return true;
    }

    uint64_t rate_limit_used = stat64_get(&mig_stats.rate_limit_used);
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

void migration_rate_reset(void)
{
    stat64_set(&mig_stats.rate_limit_used, 0);
}

void migration_rate_account(uint64_t len)
{
    stat64_add(&mig_stats.rate_limit_used, len);
}
