/*
 * Block activation tracking for migration purpose
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2024 Red Hat, Inc.
 */
#include "qemu/osdep.h"
#include "block/block.h"
#include "qapi/error.h"
#include "migration/migration.h"
#include "qemu/error-report.h"
#include "trace.h"

/*
 * Migration-only cache to remember the block layer activation status.
 * Protected by BQL.
 *
 * We need this because..
 *
 * - Migration can fail after block devices are invalidated (during
 *   switchover phase).  When that happens, we need to be able to recover
 *   the block drive status by re-activating them.
 *
 * - Currently bdrv_inactivate_all() is not safe to be invoked on top of
 *   invalidated drives (even if bdrv_activate_all() is actually safe to be
 *   called any time!).  It means remembering this could help migration to
 *   make sure it won't invalidate twice in a row, crashing QEMU.  It can
 *   happen when we migrate a PAUSED VM from host1 to host2, then migrate
 *   again to host3 without starting it.  TODO: a cleaner solution is to
 *   allow safe invoke of bdrv_inactivate_all() at anytime, like
 *   bdrv_activate_all().
 *
 * For freshly started QEMU, the flag is initialized to TRUE reflecting the
 * scenario where QEMU owns block device ownerships.
 *
 * For incoming QEMU taking a migration stream, the flag is initialized to
 * FALSE reflecting that the incoming side doesn't own the block devices,
 * not until switchover happens.
 */
static bool migration_block_active;

/* Setup the disk activation status */
void migration_block_active_setup(bool active)
{
    migration_block_active = active;
}

bool migration_block_activate(Error **errp)
{
    ERRP_GUARD();

    assert(bql_locked());

    if (migration_block_active) {
        trace_migration_block_activation("active-skipped");
        return true;
    }

    trace_migration_block_activation("active");

    bdrv_activate_all(errp);
    if (*errp) {
        error_report_err(error_copy(*errp));
        return false;
    }

    migration_block_active = true;
    return true;
}

bool migration_block_inactivate(void)
{
    int ret;

    assert(bql_locked());

    if (!migration_block_active) {
        trace_migration_block_activation("inactive-skipped");
        return true;
    }

    trace_migration_block_activation("inactive");

    ret = bdrv_inactivate_all();
    if (ret) {
        error_report("%s: bdrv_inactivate_all() failed: %d",
                     __func__, ret);
        return false;
    }

    migration_block_active = false;
    return true;
}
