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

#define MIG_STATE_ERROR		-1
#define MIG_STATE_COMPLETED	0
#define MIG_STATE_CANCELLED	1
#define MIG_STATE_ACTIVE	2

typedef struct MigrationState MigrationState;

struct MigrationState
{
    int64_t bandwidth_limit;
    QEMUFile *file;
    int fd;
    Monitor *mon;
    int state;
    int (*get_error)(MigrationState *s);
    int (*close)(MigrationState *s);
    int (*write)(MigrationState *s, const void *buff, size_t size);
    void (*cancel)(MigrationState *s);
    int (*get_status)(MigrationState *s);
    void (*release)(MigrationState *s);
    void *opaque;
    int blk;
    int shared;
};

void process_incoming_migration(QEMUFile *f);

int qemu_start_incoming_migration(const char *uri);

int do_migrate(Monitor *mon, const QDict *qdict, QObject **ret_data);

int do_migrate_cancel(Monitor *mon, const QDict *qdict, QObject **ret_data);

int do_migrate_set_speed(Monitor *mon, const QDict *qdict, QObject **ret_data);

uint64_t migrate_max_downtime(void);

int do_migrate_set_downtime(Monitor *mon, const QDict *qdict,
                            QObject **ret_data);

void do_info_migrate_print(Monitor *mon, const QObject *data);

void do_info_migrate(Monitor *mon, QObject **ret_data);

int exec_start_incoming_migration(const char *host_port);

MigrationState *exec_start_outgoing_migration(Monitor *mon,
                                              const char *host_port,
					      int64_t bandwidth_limit,
					      int detach,
					      int blk,
					      int inc);

int tcp_start_incoming_migration(const char *host_port);

MigrationState *tcp_start_outgoing_migration(Monitor *mon,
                                             const char *host_port,
					     int64_t bandwidth_limit,
					     int detach,
					     int blk,
					     int inc);

int unix_start_incoming_migration(const char *path);

MigrationState *unix_start_outgoing_migration(Monitor *mon,
                                              const char *path,
					      int64_t bandwidth_limit,
					      int detach,
					      int blk,
					      int inc);

int fd_start_incoming_migration(const char *path);

MigrationState *fd_start_outgoing_migration(Monitor *mon,
					    const char *fdname,
					    int64_t bandwidth_limit,
					    int detach,
					    int blk,
					    int inc);

void migrate_fd_error(MigrationState *s);

int migrate_fd_cleanup(MigrationState *s);

void migrate_fd_put_notify(void *opaque);

ssize_t migrate_fd_put_buffer(void *opaque, const void *data, size_t size);

void migrate_fd_connect(MigrationState *s);

void migrate_fd_put_ready(void *opaque);

void migrate_fd_wait_for_unfreeze(void *opaque);

int migrate_fd_close(void *opaque);

MigrationState *migrate_new(Monitor *mon, int64_t bandwidth_limit,
                            int detach, int blk, int inc);

void add_migration_state_change_notifier(Notifier *notify);
void remove_migration_state_change_notifier(Notifier *notify);
int get_migration_state(void);

uint64_t ram_bytes_remaining(void);
uint64_t ram_bytes_transferred(void);
uint64_t ram_bytes_total(void);

int ram_save_live(Monitor *mon, QEMUFile *f, int stage, void *opaque);
int ram_load(QEMUFile *f, void *opaque, int version_id);

extern int incoming_expected;

#endif
