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
    DPRINTF("tcp_close\n");
    if (s->fd != -1) {
        close(s->fd);
        s->fd = -1;
    }
    return 0;
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

int tcp_start_outgoing_migration(MigrationState *s, const char *host_port)
{
    struct sockaddr_in addr;
    int ret;

    ret = parse_host_port(&addr, host_port);
    if (ret < 0) {
        return ret;
    }

    s->get_error = socket_errno;
    s->write = socket_write;
    s->close = tcp_close;

    s->fd = qemu_socket(PF_INET, SOCK_STREAM, 0);
    if (s->fd == -1) {
        DPRINTF("Unable to open socket");
        return -socket_error();
    }

    socket_set_nonblock(s->fd);

    do {
        ret = connect(s->fd, (struct sockaddr *)&addr, sizeof(addr));
        if (ret == -1) {
            ret = -socket_error();
        }
        if (ret == -EINPROGRESS || ret == -EWOULDBLOCK) {
            qemu_set_fd_handler2(s->fd, NULL, NULL, tcp_wait_for_connect, s);
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

int tcp_start_incoming_migration(const char *host_port)
{
    struct sockaddr_in addr;
    int val;
    int s;

    DPRINTF("Attempting to start an incoming migration\n");

    if (parse_host_port(&addr, host_port) < 0) {
        fprintf(stderr, "invalid host/port combination: %s\n", host_port);
        return -EINVAL;
    }

    s = qemu_socket(PF_INET, SOCK_STREAM, 0);
    if (s == -1) {
        return -socket_error();
    }

    val = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&val, sizeof(val));

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        goto err;
    }
    if (listen(s, 1) == -1) {
        goto err;
    }

    qemu_set_fd_handler2(s, NULL, tcp_accept_incoming_migration, NULL,
                         (void *)(intptr_t)s);

    return 0;

err:
    close(s);
    return -socket_error();
}
