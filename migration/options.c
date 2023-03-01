/*
 * QEMU migration capabilities
 *
 * Copyright (c) 2012-2023 Red Hat Inc
 *
 * Authors:
 *   Orit Wasserman <owasserm@redhat.com>
 *   Juan Quintela <quintela@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "migration.h"
#include "options.h"

bool migrate_auto_converge(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_AUTO_CONVERGE];
}

bool migrate_background_snapshot(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_BACKGROUND_SNAPSHOT];
}

bool migrate_colo(void)
{
    MigrationState *s = migrate_get_current();
    return s->capabilities[MIGRATION_CAPABILITY_X_COLO];
}

bool migrate_compress(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_COMPRESS];
}

bool migrate_dirty_bitmaps(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_DIRTY_BITMAPS];
}

bool migrate_events(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_EVENTS];
}

bool migrate_ignore_shared(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_X_IGNORE_SHARED];
}

bool migrate_late_block_activate(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_LATE_BLOCK_ACTIVATE];
}

bool migrate_multifd(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_MULTIFD];
}

bool migrate_pause_before_switchover(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_PAUSE_BEFORE_SWITCHOVER];
}

bool migrate_postcopy_blocktime(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_POSTCOPY_BLOCKTIME];
}

bool migrate_postcopy_preempt(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_POSTCOPY_PREEMPT];
}

bool migrate_postcopy_ram(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_POSTCOPY_RAM];
}

bool migrate_release_ram(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_RELEASE_RAM];
}

bool migrate_validate_uuid(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_VALIDATE_UUID];
}

bool migrate_zero_blocks(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->capabilities[MIGRATION_CAPABILITY_ZERO_BLOCKS];
}
