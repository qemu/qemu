/*
 * QEMU live migration
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

#ifndef QEMU_MIGRATION_H
#define QEMU_MIGRATION_H

#include "qdict.h"
#include "qemu-common.h"
#include "notify.h"
#include "error.h"

typedef struct MigrationState MigrationState;

struct MigrationState
{
    int64_t bandwidth_limit;
    QEMUFile *file;
    int fd;
    int state;
    int (*get_error)(MigrationState *s);
    int (*close)(MigrationState *s);
    int (*write)(MigrationState *s, const void *buff, size_t size);
    void *opaque;
    int blk;
    int shared;
};

void process_incoming_migration(QEMUFile *f);

int qemu_start_incoming_migration(const char *uri);

uint64_t migrate_max_downtime(void);

void do_info_migrate_print(Monitor *mon, const QObject *data);

void do_info_migrate(Monitor *mon, QObject **ret_data);

int exec_start_incoming_migration(const char *host_port);

int exec_start_outgoing_migration(MigrationState *s, const char *host_port);

int tcp_start_incoming_migration(const char *host_port);

int tcp_start_outgoing_migration(MigrationState *s, const char *host_port);

int unix_start_incoming_migration(const char *path);

int unix_start_outgoing_migration(MigrationState *s, const char *path);

int fd_start_incoming_migration(const char *path);

int fd_start_outgoing_migration(MigrationState *s, const char *fdname);

void migrate_fd_error(MigrationState *s);

void migrate_fd_connect(MigrationState *s);

void add_migration_state_change_notifier(Notifier *notify);
void remove_migration_state_change_notifier(Notifier *notify);
bool migration_is_active(MigrationState *);
bool migration_has_finished(MigrationState *);
bool migration_has_failed(MigrationState *);

uint64_t ram_bytes_remaining(void);
uint64_t ram_bytes_transferred(void);
uint64_t ram_bytes_total(void);

int ram_save_live(QEMUFile *f, int stage, void *opaque);
int ram_load(QEMUFile *f, void *opaque, int version_id);

/**
 * @migrate_add_blocker - prevent migration from proceeding
 *
 * @reason - an error to be returned whenever migration is attempted
 */
void migrate_add_blocker(Error *reason);

/**
 * @migrate_del_blocker - remove a blocking error from migration
 *
 * @reason - the error blocking migration
 */
void migrate_del_blocker(Error *reason);

#endif
