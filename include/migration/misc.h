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
#include "migration/client-options.h"

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
} PrecopyNotifyData;

void precopy_infrastructure_init(void);
void precopy_add_notifier(NotifierWithReturn *n);
void precopy_remove_notifier(NotifierWithReturn *n);
int precopy_notify(PrecopyNotifyReason reason, Error **errp);

void qemu_guest_free_page_hint(void *addr, size_t len);
bool migrate_ram_is_ignored(RAMBlock *block);

/* migration/block.c */

AnnounceParameters *migrate_announce_params(void);

/* migration/savevm.c */

void dump_vmstate_json_to_file(FILE *out_fp);
void qemu_loadvm_start_load_thread(MigrationLoadThread function,
                                   void *opaque);

/* migration/migration.c */
void migration_object_init(void);
void migration_shutdown(void);

bool migration_is_running(void);
bool migration_thread_is_self(void);

typedef enum MigrationEventType {
    MIG_EVENT_PRECOPY_SETUP,
    MIG_EVENT_PRECOPY_DONE,
    MIG_EVENT_PRECOPY_FAILED,
    MIG_EVENT_MAX
} MigrationEventType;

typedef struct MigrationEvent {
    MigrationEventType type;
} MigrationEvent;

/*
 * A MigrationNotifyFunc may return an error code and an Error object,
 * but only when @e->type is MIG_EVENT_PRECOPY_SETUP.  The code is an int
 * to allow for different failure modes and recovery actions.
 */
typedef int (*MigrationNotifyFunc)(NotifierWithReturn *notify,
                                   MigrationEvent *e, Error **errp);

/*
 * Register the notifier @notify to be called when a migration event occurs
 * for MIG_MODE_NORMAL, as specified by the MigrationEvent passed to @func.
 * Notifiers may receive events in any of the following orders:
 *    - MIG_EVENT_PRECOPY_SETUP -> MIG_EVENT_PRECOPY_DONE
 *    - MIG_EVENT_PRECOPY_SETUP -> MIG_EVENT_PRECOPY_FAILED
 *    - MIG_EVENT_PRECOPY_FAILED
 */
void migration_add_notifier(NotifierWithReturn *notify,
                            MigrationNotifyFunc func);

/*
 * Same as migration_add_notifier, but applies to be specified @mode.
 */
void migration_add_notifier_mode(NotifierWithReturn *notify,
                                 MigrationNotifyFunc func, MigMode mode);

void migration_remove_notifier(NotifierWithReturn *notify);
void migration_file_set_error(int ret, Error *err);

/* True if incoming migration entered POSTCOPY_INCOMING_DISCARD */
bool migration_in_incoming_postcopy(void);

/* True if incoming migration entered POSTCOPY_INCOMING_ADVISE */
bool migration_incoming_postcopy_advised(void);

/* True if background snapshot is active */
bool migration_in_bg_snapshot(void);

/* Wrapper for block active/inactive operations */
bool migration_block_activate(Error **errp);
bool migration_block_inactivate(void);

/* True if @uri starts with a syntactically valid URI prefix */
bool migrate_is_uri(const char *uri);

/* Parse @uri and return @channel, returning true on success */
bool migrate_uri_parse(const char *uri, MigrationChannel **channel,
                       Error **errp);

/* migration/multifd-device-state.c */
typedef struct SaveLiveCompletePrecopyThreadData {
    SaveLiveCompletePrecopyThreadHandler hdlr;
    char *idstr;
    uint32_t instance_id;
    void *handler_opaque;
} SaveLiveCompletePrecopyThreadData;

bool multifd_queue_device_state(char *idstr, uint32_t instance_id,
                                char *data, size_t len);
bool multifd_device_state_supported(void);

void
multifd_spawn_device_state_save_thread(SaveLiveCompletePrecopyThreadHandler hdlr,
                                       char *idstr, uint32_t instance_id,
                                       void *opaque);

bool multifd_device_state_save_thread_should_exit(void);

void multifd_abort_device_state_save_threads(void);
bool multifd_join_device_state_save_threads(void);

#endif
