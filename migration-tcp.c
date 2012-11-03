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
    if (closesocket(s->fd) < 0) {
        r = -socket_error();
    }
    return r;
}

static void tcp_wait_for_connect(int fd, void *opaque)
{
    MigrationState *s = opaque;

    if (fd < 0) {
        DPRINTF("migrate connect error\n");
        s->fd = -1;
        migrate_fd_error(s);
    } else {
        DPRINTF("migrate connect success\n");
        s->fd = fd;
        migrate_fd_connect(s);
    }
}

void tcp_start_outgoing_migration(MigrationState *s, const char *host_port, Error **errp)
{
    s->get_error = socket_errno;
    s->write = socket_write;
    s->close = tcp_close;

    s->fd = inet_nonblocking_connect(host_port, tcp_wait_for_connect, s, errp);
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
    qemu_set_fd_handler2(s, NULL, NULL, NULL, NULL);
    closesocket(s);

    DPRINTF("accepted migration\n");

    if (c == -1) {
        fprintf(stderr, "could not accept migration connection\n");
        goto out;
    }

    f = qemu_fopen_socket(c);
    if (f == NULL) {
        fprintf(stderr, "could not qemu_fopen socket\n");
        goto out;
    }

    process_incoming_migration(f);
    return;

out:
    closesocket(c);
}

void tcp_start_incoming_migration(const char *host_port, Error **errp)
{
    int s;

    s = inet_listen(host_port, NULL, 256, SOCK_STREAM, 0, errp);
    if (s < 0) {
        return;
    }

    qemu_set_fd_handler2(s, NULL, tcp_accept_incoming_migration, NULL,
                         (void *)(intptr_t)s);
}
