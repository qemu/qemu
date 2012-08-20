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
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu-common.h"
#include "qemu_socket.h"
#include "migration.h"
#include "qemu-char.h"
#include "buffered_file.h"
#include "block.h"

//#define DEBUG_MIGRATION_TCP

#ifdef DEBUG_MIGRATION_TCP
#define DPRINTF(fmt, ...) \
    do { printf("migration-tcp: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

static int socket_errno(MigrationState *s)
{
    return socket_error();
}

static int socket_write(MigrationState *s, const void * buf, size_t size)
{
    return send(s->fd, buf, size, 0);
}

static int tcp_close(MigrationState *s)
{
    int r = 0;
    DPRINTF("tcp_close\n");
    if (s->fd != -1) {
        if (close(s->fd) < 0) {
            r = -errno;
        }
        s->fd = -1;
    }
    return r;
}

static void tcp_wait_for_connect(void *opaque)
{
    MigrationState *s = opaque;
    int val, ret;
    socklen_t valsize = sizeof(val);

    DPRINTF("connect completed\n");
    do {
        ret = getsockopt(s->fd, SOL_SOCKET, SO_ERROR, (void *) &val, &valsize);
    } while (ret == -1 && (socket_error()) == EINTR);

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

int tcp_start_outgoing_migration(MigrationState *s, const char *host_port,
                                 Error **errp)
{
    bool in_progress;

    s->get_error = socket_errno;
    s->write = socket_write;
    s->close = tcp_close;

    s->fd = inet_connect(host_port, false, &in_progress, errp);
    if (error_is_set(errp)) {
        migrate_fd_error(s);
        return -1;
    }

    if (in_progress) {
        DPRINTF("connect in progress\n");
        qemu_set_fd_handler2(s->fd, NULL, NULL, tcp_wait_for_connect, s);
    } else {
        migrate_fd_connect(s);
    }

    return 0;
}

static void tcp_accept_incoming_migration(void *opaque)
{
    struct sockaddr_in addr;
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

int tcp_start_incoming_migration(const char *host_port, Error **errp)
{
    int s;

    s = inet_listen(host_port, NULL, 256, SOCK_STREAM, 0, errp);

    if (s < 0) {
        return -1;
    }

    qemu_set_fd_handler2(s, NULL, tcp_accept_incoming_migration, NULL,
                         (void *)(intptr_t)s);

    return 0;
}
