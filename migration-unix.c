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
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu-common.h"
#include "qemu/sockets.h"
#include "migration/migration.h"
#include "migration/qemu-file.h"
#include "block/block.h"

//#define DEBUG_MIGRATION_UNIX

#ifdef DEBUG_MIGRATION_UNIX
#define DPRINTF(fmt, ...) \
    do { printf("migration-unix: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

static void unix_wait_for_connect(int fd, void *opaque)
{
    MigrationState *s = opaque;

    if (fd < 0) {
        DPRINTF("migrate connect error\n");
        s->file = NULL;
        migrate_fd_error(s);
    } else {
        DPRINTF("migrate connect success\n");
        s->file = qemu_fopen_socket(fd, "wb");
        migrate_fd_connect(s);
    }
}

void unix_start_outgoing_migration(MigrationState *s, const char *path, Error **errp)
{
    unix_nonblocking_connect(path, unix_wait_for_connect, s, errp);
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
    qemu_set_fd_handler2(s, NULL, NULL, NULL, NULL);
    close(s);

    DPRINTF("accepted migration\n");

    if (c == -1) {
        fprintf(stderr, "could not accept migration connection\n");
        goto out;
    }

    f = qemu_fopen_socket(c, "rb");
    if (f == NULL) {
        fprintf(stderr, "could not qemu_fopen socket\n");
        goto out;
    }

    process_incoming_migration(f);
    return;

out:
    close(c);
}

void unix_start_incoming_migration(const char *path, Error **errp)
{
    int s;

    s = unix_listen(path, NULL, 0, errp);
    if (s < 0) {
        return;
    }

    qemu_set_fd_handler2(s, NULL, unix_accept_incoming_migration, NULL,
                         (void *)(intptr_t)s);
}
