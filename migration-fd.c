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
 */

#include "qemu-common.h"
#include "qemu_socket.h"
#include "migration.h"
#include "monitor.h"
#include "qemu-char.h"
#include "buffered_file.h"
#include "block.h"
#include "qemu_socket.h"

//#define DEBUG_MIGRATION_FD

#ifdef DEBUG_MIGRATION_FD
#define DPRINTF(fmt, ...) \
    do { printf("migration-fd: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

static int fd_errno(FdMigrationState *s)
{
    return errno;
}

static int fd_write(FdMigrationState *s, const void * buf, size_t size)
{
    return write(s->fd, buf, size);
}

static int fd_close(FdMigrationState *s)
{
    DPRINTF("fd_close\n");
    if (s->fd != -1) {
        close(s->fd);
        s->fd = -1;
    }
    return 0;
}

MigrationState *fd_start_outgoing_migration(Monitor *mon,
					    const char *fdname,
					    int64_t bandwidth_limit,
					    int detach,
					    int blk,
					    int inc)
{
    FdMigrationState *s;

    s = qemu_mallocz(sizeof(*s));

    s->fd = monitor_get_fd(mon, fdname);
    if (s->fd == -1) {
        DPRINTF("fd_migration: invalid file descriptor identifier\n");
        goto err_after_alloc;
    }

    if (fcntl(s->fd, F_SETFL, O_NONBLOCK) == -1) {
        DPRINTF("Unable to set nonblocking mode on file descriptor\n");
        goto err_after_open;
    }

    s->get_error = fd_errno;
    s->write = fd_write;
    s->close = fd_close;
    s->mig_state.cancel = migrate_fd_cancel;
    s->mig_state.get_status = migrate_fd_get_status;
    s->mig_state.release = migrate_fd_release;

    s->mig_state.blk = blk;
    s->mig_state.shared = inc;

    s->state = MIG_STATE_ACTIVE;
    s->mon = NULL;
    s->bandwidth_limit = bandwidth_limit;

    if (!detach) {
        migrate_fd_monitor_suspend(s, mon);
    }

    migrate_fd_connect(s);
    return &s->mig_state;

err_after_open:
    close(s->fd);
err_after_alloc:
    qemu_free(s);
    return NULL;
}

static void fd_accept_incoming_migration(void *opaque)
{
    QEMUFile *f = opaque;

    process_incoming_migration(f);
    qemu_set_fd_handler2(qemu_stdio_fd(f), NULL, NULL, NULL, NULL);
    qemu_fclose(f);
}

int fd_start_incoming_migration(const char *infd)
{
    int fd;
    QEMUFile *f;

    DPRINTF("Attempting to start an incoming migration via fd\n");

    fd = strtol(infd, NULL, 0);
    f = qemu_fdopen(fd, "rb");
    if(f == NULL) {
        DPRINTF("Unable to apply qemu wrapper to file descriptor\n");
        return -errno;
    }

    qemu_set_fd_handler2(fd, NULL, fd_accept_incoming_migration, NULL, f);

    return 0;
}
