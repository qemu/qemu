/*
 * QEMU live migration via generic fd
 *
 * Copyright Red Hat, Inc. 2009
 *
 * Authors:
 *  Chris Lalancette <clalance@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu-common.h"
#include "qemu/main-loop.h"
#include "qemu/sockets.h"
#include "migration/migration.h"
#include "monitor/monitor.h"
#include "migration/qemu-file.h"
#include "block/block.h"

//#define DEBUG_MIGRATION_FD

#ifdef DEBUG_MIGRATION_FD
#define DPRINTF(fmt, ...) \
    do { printf("migration-fd: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

void fd_start_outgoing_migration(MigrationState *s, const char *fdname, Error **errp)
{
    int fd = monitor_get_fd(cur_mon, fdname, errp);
    if (fd == -1) {
        return;
    }
    s->file = qemu_fdopen(fd, "wb");

    migrate_fd_connect(s);
}

static void fd_accept_incoming_migration(void *opaque)
{
    QEMUFile *f = opaque;

    qemu_set_fd_handler2(qemu_get_fd(f), NULL, NULL, NULL, NULL);
    process_incoming_migration(f);
}

void fd_start_incoming_migration(const char *infd, Error **errp)
{
    int fd;
    QEMUFile *f;

    DPRINTF("Attempting to start an incoming migration via fd\n");

    fd = strtol(infd, NULL, 0);
    f = qemu_fdopen(fd, "rb");
    if(f == NULL) {
        error_setg_errno(errp, errno, "failed to open the source descriptor");
        return;
    }

    qemu_set_fd_handler2(fd, NULL, fd_accept_incoming_migration, NULL, f);
}
