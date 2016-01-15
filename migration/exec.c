/*
 * QEMU live migration
 *
 * Copyright IBM, Corp. 2008
 * Copyright Dell MessageOne 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Charles Duffy     <charles_duffy@messageone.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/sockets.h"
#include "qemu/main-loop.h"
#include "migration/migration.h"
#include "migration/qemu-file.h"
#include "block/block.h"
#include <sys/wait.h>

//#define DEBUG_MIGRATION_EXEC

#ifdef DEBUG_MIGRATION_EXEC
#define DPRINTF(fmt, ...) \
    do { printf("migration-exec: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

void exec_start_outgoing_migration(MigrationState *s, const char *command, Error **errp)
{
    s->to_dst_file = qemu_popen_cmd(command, "w");
    if (s->to_dst_file == NULL) {
        error_setg_errno(errp, errno, "failed to popen the migration target");
        return;
    }

    migrate_fd_connect(s);
}

static void exec_accept_incoming_migration(void *opaque)
{
    QEMUFile *f = opaque;

    qemu_set_fd_handler(qemu_get_fd(f), NULL, NULL, NULL);
    process_incoming_migration(f);
}

void exec_start_incoming_migration(const char *command, Error **errp)
{
    QEMUFile *f;

    DPRINTF("Attempting to start an incoming migration\n");
    f = qemu_popen_cmd(command, "r");
    if(f == NULL) {
        error_setg_errno(errp, errno, "failed to popen the migration source");
        return;
    }

    qemu_set_fd_handler(qemu_get_fd(f), exec_accept_incoming_migration, NULL,
                        f);
}
