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
#include "qapi/qapi-types-migration.h"
#include "qapi/qapi-types-net.h"

/* migration/ram.c */

typedef enum PrecopyNotifyReason {
    PRECOPY_NOTIFY_SETUP = 0,
    PRECOPY_NOTIFY_BEFORE_BITMAP_SYNC = 1,
    PRECOPY_NOTIFY_AFTER_BITMAP_SYNC = 2,
    PRECOPY_NOTIFY_COMPLETE = 3,
    PRECOPY_NOTIFY_CLEANUP = 4,
    PRECOPY_NOTIFY_MAX = 5,
} PrecopyNotifyReason;

typedef struct PrecopyNotifyData {
    enum PrecopyNotifyReason reason;
    Error **errp;
} PrecopyNotifyData;

void precopy_infrastructure_init(void);
void precopy_add_notifier(NotifierWithReturn *n);
void precopy_remove_notifier(NotifierWithReturn *n);
int precopy_notify(PrecopyNotifyReason reason, Error **errp);

void ram_mig_init(void);
void qemu_guest_free_page_hint(void *addr, size_t len);
bool migrate_ram_is_ignored(RAMBlock *block);

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
bool migration_is_idle(void);
bool migration_is_active(MigrationState *);
void migration_add_notifier(Notifier *notify,
                            void (*func)(Notifier *notifier, void *data));
void migration_remove_notifier(Notifier *notify);
void migration_call_notifiers(MigrationState *s);
bool migration_in_setup(MigrationState *);
bool migration_has_finished(MigrationState *);
bool migration_has_failed(MigrationState *);
/* ...and after the device transmission */
bool migration_in_postcopy_after_devices(MigrationState *);
/* True if incoming migration entered POSTCOPY_INCOMING_DISCARD */
bool migration_in_incoming_postcopy(void);
/* True if incoming migration entered POSTCOPY_INCOMING_ADVISE */
bool migration_incoming_postcopy_advised(void);
/* True if background snapshot is active */
bool migration_in_bg_snapshot(void);

/* migration/block-dirty-bitmap.c */
void dirty_bitmap_mig_init(void);

#endif
