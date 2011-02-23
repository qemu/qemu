/*
 * QEMU live migration via Unix Domain Sockets
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
#include "qemu-char.h"
#include "buffered_file.h"
#include "block.h"

//#define DEBUG_MIGRATION_UNIX

#ifdef DEBUG_MIGRATION_UNIX
#define DPRINTF(fmt, ...) \
    do { printf("migration-unix: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

static int unix_errno(MigrationState *s)
{
    return errno;
}

static int unix_write(MigrationState *s, const void * buf, size_t size)
{
    return write(s->fd, buf, size);
}

static int unix_close(MigrationState *s)
{
    DPRINTF("unix_close\n");
    if (s->fd != -1) {
        close(s->fd);
        s->fd = -1;
    }
    return 0;
}

static void unix_wait_for_connect(void *opaque)
{
    MigrationState *s = opaque;
    int val, ret;
    socklen_t valsize = sizeof(val);

    DPRINTF("connect completed\n");
    do {
        ret = getsockopt(s->fd, SOL_SOCKET, SO_ERROR, (void *) &val, &valsize);
    } while (ret == -1 && errno == EINTR);

    if (ret < 0) {
        migrate_fd_error(s);
        return;
    }

    qemu_set_fd_handler2(s->fd, NULL, NULL, NULL, NULL);

    if (val == 0)
        migrate_fd_connect(s);
    else {
        DPRINTF("error connecting %d\n", val);
        migrate_fd_error(s);
    }
}

int unix_start_outgoing_migration(MigrationState *s, const char *path)
{
    struct sockaddr_un addr;
    int ret;

    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    s->get_error = unix_errno;
    s->write = unix_write;
    s->close = unix_close;

    s->fd = qemu_socket(PF_UNIX, SOCK_STREAM, 0);
    if (s->fd == -1) {
        DPRINTF("Unable to open socket");
        return -errno;
    }

    socket_set_nonblock(s->fd);

    do {
        ret = connect(s->fd, (struct sockaddr *)&addr, sizeof(addr));
        if (ret == -1) {
            ret = -errno;
        }
        if (ret == -EINPROGRESS || ret == -EWOULDBLOCK) {
	    qemu_set_fd_handler2(s->fd, NULL, NULL, unix_wait_for_connect, s);
            return 0;
        }
    } while (ret == -EINTR);

    if (ret < 0) {
        DPRINTF("connect failed\n");
        migrate_fd_error(s);
        return ret;
    }
    migrate_fd_connect(s);
    return 0;
}

static void unix_accept_incoming_migration(void *opaque)
{
    struct sockaddr_un addr;
    socklen_t addrlen = sizeof(addr);
    int s = (intptr_t)opaque;
    QEMUFile *f;
    int c;

    do {
        c = qemu_accept(s, (struct sockaddr *)&addr, &addrlen);
    } while (c == -1 && errno == EINTR);

    DPRINTF("accepted migration\n");

    if (c == -1) {
        fprintf(stderr, "could not accept migration connection\n");
        goto out2;
    }

    f = qemu_fopen_socket(c);
    if (f == NULL) {
        fprintf(stderr, "could not qemu_fopen socket\n");
        goto out;
    }

    process_incoming_migration(f);
    qemu_fclose(f);
out:
    close(c);
out2:
    qemu_set_fd_handler2(s, NULL, NULL, NULL, NULL);
    close(s);
}

int unix_start_incoming_migration(const char *path)
{
    struct sockaddr_un addr;
    int s;
    int ret;

    DPRINTF("Attempting to start an incoming migration\n");

    s = qemu_socket(PF_UNIX, SOCK_STREAM, 0);
    if (s == -1) {
        fprintf(stderr, "Could not open unix socket: %s\n", strerror(errno));
        return -errno;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    unlink(addr.sun_path);
    if (bind(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        ret = -errno;
        fprintf(stderr, "bind(unix:%s): %s\n", addr.sun_path, strerror(errno));
        goto err;
    }
    if (listen(s, 1) == -1) {
        fprintf(stderr, "listen(unix:%s): %s\n", addr.sun_path,
                strerror(errno));
        ret = -errno;
        goto err;
    }

    qemu_set_fd_handler2(s, NULL, unix_accept_incoming_migration, NULL,
                         (void *)(intptr_t)s);

    return 0;

err:
    close(s);
    return ret;
}
