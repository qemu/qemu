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

#include "exec/cpu-common.h"
#include "qemu/notify.h"
#include "qapi/qapi-types-net.h"

/* migration/ram.c */

void ram_mig_init(void);
void qemu_guest_free_page_hint(void *addr, size_t len);

/* migration/block.c */

#ifdef CONFIG_LIVE_BLOCK_MIGRATION
void blk_mig_init(void);
#else
static inline void blk_mig_init(void) {}
#endif

AnnounceParameters *migrate_announce_params(void);
/* migration/savevm.c */

void dump_vmstate_json_to_file(FILE *out_fp);

/* migration/migration.c */
void migration_object_init(void);
void migration_shutdown(void);
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

/* migration/block-dirty-bitmap.c */
void dirty_bitmap_mig_init(void);

#endif
