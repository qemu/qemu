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

static int fd_errno(MigrationState *s)
{
    return errno;
}

static int fd_write(MigrationState *s, const void * buf, size_t size)
{
    return write(s->fd, buf, size);
}

static int fd_close(MigrationState *s)
{
    struct stat st;
    int ret;

    DPRINTF("fd_close\n");
    if (s->fd != -1) {
        ret = fstat(s->fd, &st);
        if (ret == 0 && S_ISREG(st.st_mode)) {
            /*
             * If the file handle is a regular file make sure the
             * data is flushed to disk before signaling success.
             */
            ret = fsync(s->fd);
            if (ret != 0) {
                ret = -errno;
                perror("migration-fd: fsync");
                return ret;
            }
        }
        ret = close(s->fd);
        s->fd = -1;
        if (ret != 0) {
            ret = -errno;
            perror("migration-fd: close");
            return ret;
        }
    }
    return 0;
}

int fd_start_outgoing_migration(MigrationState *s, const char *fdname)
{
    s->fd = monitor_get_fd(s->mon, fdname);
    if (s->fd == -1) {
        DPRINTF("fd_migration: invalid file descriptor identifier\n");
        goto err_after_get_fd;
    }

    if (fcntl(s->fd, F_SETFL, O_NONBLOCK) == -1) {
        DPRINTF("Unable to set nonblocking mode on file descriptor\n");
        goto err_after_open;
    }

    s->get_error = fd_errno;
    s->write = fd_write;
    s->close = fd_close;

    migrate_fd_connect(s);
    return 0;

err_after_open:
    close(s->fd);
err_after_get_fd:
    return -1;
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
