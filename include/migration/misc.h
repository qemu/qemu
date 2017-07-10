/*
 * QEMU migration miscellaneus exported functions
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef MIGRATION_MISC_H
#define MIGRATION_MISC_H

#include "qemu/notify.h"

/* migration/ram.c */

void ram_mig_init(void);

/* migration/block.c */

#ifdef CONFIG_LIVE_BLOCK_MIGRATION
void blk_mig_init(void);
#else
static inline void blk_mig_init(void) {}
#endif

#define SELF_ANNOUNCE_ROUNDS 5

static inline
int64_t self_announce_delay(int round)
{
    assert(round < SELF_ANNOUNCE_ROUNDS && round > 0);
    /* delay 50ms, 150ms, 250ms, ... */
    return 50 + (SELF_ANNOUNCE_ROUNDS - round - 1) * 100;
}

/* migration/savevm.c */

void dump_vmstate_json_to_file(FILE *out_fp);

/* migration/migration.c */
void migration_object_init(void);
void qemu_start_incoming_migration(const char *uri, Error **errp);
bool migration_is_idle(void);
void add_migration_state_change_notifier(Notifier *notify);
void remove_migration_state_change_notifier(Notifier *notify);
bool migration_in_setup(MigrationState *);
bool migration_has_finished(MigrationState *);
bool migration_has_failed(MigrationState *);
/* ...and after the device transmission */
bool migration_in_postcopy_after_devices(MigrationState *);
void migration_global_dump(Monitor *mon);

#endif
