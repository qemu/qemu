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

static int unix_errno(FdMigrationState *s)
{
    return errno;
}

static int unix_write(FdMigrationState *s, const void * buf, size_t size)
{
    return write(s->fd, buf, size);
}

static int unix_close(FdMigrationState *s)
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
    FdMigrationState *s = opaque;
    int val, ret;
    socklen_t valsize = sizeof(val);

    DPRINTF("connect completed\n");
    do {
        ret = getsockopt(s->fd, SOL_SOCKET, SO_ERROR, (void *) &val, &valsize);
    } while (ret == -1 && (s->get_error(s)) == EINTR);

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

MigrationState *unix_start_outgoing_migration(Monitor *mon,
                                              const char *path,
					      int64_t bandwidth_limit,
					      int detach,
					      int blk,
					      int inc)
{
    FdMigrationState *s;
    struct sockaddr_un addr;
    int ret;

    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    s = qemu_mallocz(sizeof(*s));

    s->get_error = unix_errno;
    s->write = unix_write;
    s->close = unix_close;
    s->mig_state.cancel = migrate_fd_cancel;
    s->mig_state.get_status = migrate_fd_get_status;
    s->mig_state.release = migrate_fd_release;

    s->mig_state.blk = blk;
    s->mig_state.shared = inc;

    s->state = MIG_STATE_ACTIVE;
    s->mon = NULL;
    s->bandwidth_limit = bandwidth_limit;
    s->fd = qemu_socket(PF_UNIX, SOCK_STREAM, 0);
    if (s->fd < 0) {
        DPRINTF("Unable to open socket");
        goto err_after_alloc;
    }

    socket_set_nonblock(s->fd);

    do {
        ret = connect(s->fd, (struct sockaddr *)&addr, sizeof(addr));
        if (ret == -1)
	    ret = -(s->get_error(s));

        if (ret == -EINPROGRESS || ret == -EWOULDBLOCK)
	    qemu_set_fd_handler2(s->fd, NULL, NULL, unix_wait_for_connect, s);
    } while (ret == -EINTR);

    if (ret < 0 && ret != -EINPROGRESS && ret != -EWOULDBLOCK) {
        DPRINTF("connect failed\n");
        goto err_after_open;
    }

    if (!detach) {
        migrate_fd_monitor_suspend(s, mon);
    }

    if (ret >= 0)
        migrate_fd_connect(s);

    return &s->mig_state;

err_after_open:
    close(s->fd);

err_after_alloc:
    qemu_free(s);
    return NULL;
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
    } while (c == -1 && socket_error() == EINTR);

    DPRINTF("accepted migration\n");

    if (c == -1) {
        fprintf(stderr, "could not accept migration connection\n");
        return;
    }

    f = qemu_fopen_socket(c);
    if (f == NULL) {
        fprintf(stderr, "could not qemu_fopen socket\n");
        goto out;
    }

    process_incoming_migration(f);
    qemu_fclose(f);
out:
    qemu_set_fd_handler2(s, NULL, NULL, NULL, NULL);
    close(s);
    close(c);
}

int unix_start_incoming_migration(const char *path)
{
    struct sockaddr_un un;
    int sock;

    DPRINTF("Attempting to start an incoming migration\n");

    sock = qemu_socket(PF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "Could not open unix socket: %s\n", strerror(errno));
        return -EINVAL;
    }

    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    snprintf(un.sun_path, sizeof(un.sun_path), "%s", path);

    unlink(un.sun_path);
    if (bind(sock, (struct sockaddr*) &un, sizeof(un)) < 0) {
        fprintf(stderr, "bind(unix:%s): %s\n", un.sun_path, strerror(errno));
        goto err;
    }
    if (listen(sock, 1) < 0) {
        fprintf(stderr, "listen(unix:%s): %s\n", un.sun_path, strerror(errno));
        goto err;
    }

    qemu_set_fd_handler2(sock, NULL, unix_accept_incoming_migration, NULL,
			 (void *)(intptr_t)sock);

    return 0;

err:
    close(sock);

    return -EINVAL;
}
