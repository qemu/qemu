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

bool migration_block_activate(Error **errp)
{
    ERRP_GUARD();

    assert(bql_locked());

    trace_migration_block_activation("active");

    bdrv_activate_all(errp);
    if (*errp) {
        error_report_err(error_copy(*errp));
        return false;
    }

    return true;
}

bool migration_block_inactivate(void)
{
    int ret;

    assert(bql_locked());

    trace_migration_block_activation("inactive");

    ret = bdrv_inactivate_all();
    if (ret) {
        error_report("%s: bdrv_inactivate_all() failed: %d",
                     __func__, ret);
        return false;
    }

    return true;
}
