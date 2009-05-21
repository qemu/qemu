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
#include "sysemu.h"
#include "buffered_file.h"
#include "block.h"

//#define DEBUG_MIGRATION_TCP

#ifdef DEBUG_MIGRATION_TCP
#define dprintf(fmt, ...) \
    do { printf("migration-tcp: " fmt, ## __VA_ARGS__); } while (0)
#else
#define dprintf(fmt, ...) \
    do { } while (0)
#endif

static int socket_errno(FdMigrationState *s)
{
    return socket_error();
}

static int socket_write(FdMigrationState *s, const void * buf, size_t size)
{
    return send(s->fd, buf, size, 0);
}

static int tcp_close(FdMigrationState *s)
{
    dprintf("tcp_close\n");
    if (s->fd != -1) {
        close(s->fd);
        s->fd = -1;
    }
    return 0;
}


static void tcp_wait_for_connect(void *opaque)
{
    FdMigrationState *s = opaque;
    int val, ret;
    socklen_t valsize = sizeof(val);

    dprintf("connect completed\n");
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
        dprintf("error connecting %d\n", val);
        migrate_fd_error(s);
    }
}

MigrationState *tcp_start_outgoing_migration(const char *host_port,
                                             int64_t bandwidth_limit,
                                             int detach)
{
    struct sockaddr_in addr;
    FdMigrationState *s;
    int ret;

    if (parse_host_port(&addr, host_port) < 0)
        return NULL;

    s = qemu_mallocz(sizeof(*s));

    s->get_error = socket_errno;
    s->write = socket_write;
    s->close = tcp_close;
    s->mig_state.cancel = migrate_fd_cancel;
    s->mig_state.get_status = migrate_fd_get_status;
    s->mig_state.release = migrate_fd_release;

    s->state = MIG_STATE_ACTIVE;
    s->mon_resume = NULL;
    s->bandwidth_limit = bandwidth_limit;
    s->fd = socket(PF_INET, SOCK_STREAM, 0);
    if (s->fd == -1) {
        qemu_free(s);
        return NULL;
    }

    socket_set_nonblock(s->fd);

    if (!detach)
        migrate_fd_monitor_suspend(s);

    do {
        ret = connect(s->fd, (struct sockaddr *)&addr, sizeof(addr));
        if (ret == -1)
            ret = -(s->get_error(s));

        if (ret == -EINPROGRESS || ret == -EWOULDBLOCK)
            qemu_set_fd_handler2(s->fd, NULL, NULL, tcp_wait_for_connect, s);
    } while (ret == -EINTR);

    if (ret < 0 && ret != -EINPROGRESS && ret != -EWOULDBLOCK) {
        dprintf("connect failed\n");
        close(s->fd);
        qemu_free(s);
        return NULL;
    } else if (ret >= 0)
        migrate_fd_connect(s);

    return &s->mig_state;
}

static void tcp_accept_incoming_migration(void *opaque)
{
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    int s = (unsigned long)opaque;
    QEMUFile *f;
    int c, ret;

    do {
        c = accept(s, (struct sockaddr *)&addr, &addrlen);
    } while (c == -1 && socket_error() == EINTR);

    dprintf("accepted migration\n");

    if (c == -1) {
        fprintf(stderr, "could not accept migration connection\n");
        return;
    }

    f = qemu_fopen_socket(c);
    if (f == NULL) {
        fprintf(stderr, "could not qemu_fopen socket\n");
        goto out;
    }

    vm_stop(0); /* just in case */
    ret = qemu_loadvm_state(f);
    if (ret < 0) {
        fprintf(stderr, "load of migration failed\n");
        goto out_fopen;
    }
    qemu_announce_self();
    dprintf("successfully loaded vm state\n");

    /* we've successfully migrated, close the server socket */
    qemu_set_fd_handler2(s, NULL, NULL, NULL, NULL);
    close(s);

    vm_start();

out_fopen:
    qemu_fclose(f);
out:
    close(c);
}

int tcp_start_incoming_migration(const char *host_port)
{
    struct sockaddr_in addr;
    int val;
    int s;

    if (parse_host_port(&addr, host_port) < 0) {
        fprintf(stderr, "invalid host/port combination: %s\n", host_port);
        return -EINVAL;
    }

    s = socket(PF_INET, SOCK_STREAM, 0);
    if (s == -1)
        return -socket_error();

    val = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&val, sizeof(val));

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == -1)
        goto err;

    if (listen(s, 1) == -1)
        goto err;

    qemu_set_fd_handler2(s, NULL, tcp_accept_incoming_migration, NULL,
                         (void *)(unsigned long)s);

    return 0;

err:
    close(s);
    return -socket_error();
}
